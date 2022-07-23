#pragma once

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <iostream>
#include <fstream>
#include <filesystem>
#include <thread>
#include <httplib.h>
#include <bxzstr.hpp>
#include <tar/tar.hpp>
#include <ar/ar.hpp>
#include <map>
#include <set>
#include <sstream>
#include <boost/regex.hpp>
#include <deb-downloader.hpp>

namespace deb{
    namespace {
        using namespace std;
        using namespace httplib;
        namespace fs = std::filesystem;

        tuple<string,string,string> splitUrl(string url){
            std::string delimiter = " ";
            std::string scheme = "";
            std::string host = "";
            std::string path = "";

            size_t pos = 0;
            std::string token;

            delimiter = "://";
            if ((pos = url.find(delimiter)) != std::string::npos) {
                token = url.substr(0, pos + delimiter.length());
                scheme = token;
                url.erase(0, pos + delimiter.length());
            }

            delimiter = "/";
            if ((pos = url.find(delimiter)) != std::string::npos) {
                token = url.substr(0, pos);
                host = token;
                url.erase(0, pos);
            }

            path = url;

            return make_tuple(scheme,host,path);
        }

        std::string downloadString(string url){
            std::string scheme = "";
            std::string host = "";
            std::string path = "";

            tie(scheme, host, path) = splitUrl(url);

            httplib::Client cli((scheme+host).c_str());
            cli.set_follow_location(true);

            auto res = cli.Get(path.c_str());

            return res->body;
        }

        std::filesystem::path downloadFile(string url, std::filesystem::path location){
            std::string scheme = "";
            std::string host = "";
            std::string path = "";

            tie(scheme, host, path) = splitUrl(url);

            std::filesystem::path extractFilename = path;
            std::filesystem::path filename = extractFilename.filename();

            fs::create_directories(location);
            
            ofstream file(location/filename);

            httplib::Client cli((scheme+host).c_str());
            cli.set_follow_location(true);

            auto res = cli.Get(path.c_str(), Headers(),
            [&](const Response &response) {
                return true; // return 'false' if you want to cancel the request.
            },
            [&](const char *data, size_t data_length) {
                file.write(data, data_length);
                return true; // return 'false' if you want to cancel the request.
            });

            file.close();
            return location/filename;
        }

        std::vector<std::string> split(const string& input, const string& regex) {
            // passing -1 as the submatch index parameter performs splitting
            static map<string, boost::regex> rgx;
            if(!rgx.count(regex)) rgx[regex] = boost::regex(regex, boost::regex::optimize);
            boost::sregex_token_iterator
                first{input.begin(), input.end(), rgx[regex], -1},
                last;
            return {first, last};
        }

        string streamToString(istream& is){
            stringstream ss;
            ss << is.rdbuf();
            return ss.str();
        }
    };

    using namespace std;

    class Installer{
    public:
        set<string> sourcesList;
        map<string,string> packageToUrl;

        // deb http://archive.ubuntu.com/ubuntu focal-updates main restricted universe multiverse
        set<tuple<string,string>> getListUrls(){
            set<tuple<string,string>> result;
            for(auto source : sourcesList) {
                stringstream ss(source);
                string token;
                if(!(ss >> token)) continue;
                if(token != "deb") continue;
                string baseUrl;
                if(!(ss >> baseUrl)) continue;
                string distribution;
                if(!(ss >> distribution)) continue;
                
                string component;
                //http://archive.ubuntu.com/ubuntu/dists/focal/main/binary-amd64/Packages.gz
                while(ss >> component){
                    result.insert({
                        baseUrl, 
                        baseUrl+"/dists/"+distribution+"/"+component+
                        "/"+architecture+"/"+"Packages.gz"
                    });
                }
                
            }
            for(const auto& elem : result)
                std::cout << get<1>(elem) << "\n";
            return result;
        }
        void getPackageList(){
            auto urls = getListUrls();
            for(const auto& entry : urls){
                string listUrl;
                string baseUrl;
                tie(baseUrl, listUrl) = entry;

                stringstream compressed(downloadString(listUrl));
                bxz::istream decompressed(compressed);
                stringstream ss;
                ss << decompressed.rdbuf();

                vector<string> entries = split(ss.str(), "\n\n");
                cout << entries.size() << "\n";

                for(string entry : entries){
                    string packageName = "";
                    string packagePath = "";
                    {
                        static boost::regex rgx("Package:\\s?([^\\r\\n]*)", boost::regex::optimize);
                        boost::smatch matches;
                        if(boost::regex_search(entry, matches, rgx) && matches.size() == 2) {
                            packageName = matches[1];
                        } else {
                            std::cout << "Match not found\n";
                            continue;
                        }
                    }
                    {
                        static boost::regex rgx("Filename:\\s?([^\\r\\n]*)", boost::regex::optimize);
                        boost::smatch matches;
                        if(boost::regex_search(entry, matches, rgx) && matches.size() == 2) {
                            packagePath = matches[1];
                        } else {
                            std::cout << "Match not found\n";
                            continue;
                        }
                    }
                    packagePath=baseUrl+"/"+packagePath;
                    auto provides = getFields(entry, "Provides");
                    auto source = getFields(entry, "Source");
                    provides.insert(source.begin(), source.end());
                    provides.insert(packageName);
                    for(auto i : provides){
                        if(!packageToUrl.count(i)){
                            packageToUrl[i] = packagePath;
                        }
                    }
                }
            }
            // for(const auto& elem : packageToUrl){
            //     std::cout << elem.first << " -> " << elem.second << "\n";
            // }
        }
        set<string> getFields(string& contolFile, string typeOfDep = "Depends"){
            set<string> result;
            static map<string, boost::regex> rgx;
            if(!rgx.count(typeOfDep)) rgx[typeOfDep] = boost::regex(typeOfDep+": ?([^\\r\\n]*)", boost::regex::optimize);
            string depends;
            {
                boost::smatch matches;
                if(boost::regex_search(contolFile, matches, rgx[typeOfDep]) && matches.size() == 2) {
                    depends = string(matches[1]);
                } else {
                    return result;
                }
            }
            auto entries = split(depends, ",|\\|");
            static boost::regex r("(?:\\s+)|(?:\\(.*\\))|(?::.*)", boost::regex::optimize);
            for(auto entry : entries){
                // (\S+)\s?(?:\((\S*)\s?(\S*)\))?
                result.insert(boost::regex_replace(entry, r, ""));
            }
            return result;
        }
        void installPrivate(string package, string location, set<string>& installed){
            if(installed.count(package)){
                cout << "already installed " << package << endl;
                return;
            }
            if(packageToUrl.empty()) getPackageList();
            if(!packageToUrl.count(package)) throw runtime_error("package "+package+" does not exist in repository.");
            string url = packageToUrl[package];
            auto packageLoc = downloadFile(url, "./tmp");
            ar::Reader deb(packageLoc.string());
            deb.allowSeekg = true;
            auto versionStream = deb.getFileStream("debian-binary");
            string version = streamToString(versionStream);
            if(version.find("2.0") == string::npos) throw runtime_error("package "+package+" has a bad version number "+version+".");

            try{
                deb.reset();
                auto dataTarCompressedStream = deb.getFileStream("data.tar.xz");
                bxz::istream dataTarStream(dataTarCompressedStream);
                tar::Reader dataTar(dataTarStream);
                dataTar.throwOnUnsupported = false;
                dataTar.extractAll(location);
            }catch(...){
                deb.reset();
                auto dataTarCompressedStream = deb.getFileStream("data.tar.gz");
                bxz::istream dataTarStream(dataTarCompressedStream);
                tar::Reader dataTar(dataTarStream);
                dataTar.throwOnUnsupported = false;
                dataTar.extractAll(location);
            }

            installed.insert(package);
            cout << "installed " << package << "\n";
            if(!recursive) return;
            string controlString;
            try{
                deb.reset();
                auto controlTarCompressedStream = deb.getFileStream("control.tar.xz");
                bxz::istream controlTarStream(controlTarCompressedStream);
                tar::Reader controlTar(controlTarStream);
                auto controlFile = controlTar.getFileStream("control");
                controlString = streamToString(controlFile);
            }catch(...){
                deb.reset();
                auto controlTarCompressedStream = deb.getFileStream("control.tar.gz");
                bxz::istream controlTarStream(controlTarCompressedStream);
                tar::Reader controlTar(controlTarStream);
                auto controlFile = controlTar.getFileStream("control");
                controlString = streamToString(controlFile);
            }
            
           // cout << "\n" << controlString << "\n";

            auto deps = getFields(controlString);
            for(auto dep : deps){
                try{
                    installPrivate(dep, location, installed);
                }catch(runtime_error e){
                    if(throwOnFailedDependency) throw e;
                    cout << "[ERROR]" << e.what() << endl;
                }   
            }
        }
    public:
        string architecture = "binary-amd64";
        bool recursive = true;
        bool throwOnFailedDependency = false;
        Installer(set<string> l) : sourcesList(l) {}
        void install(string package, string location){
            set<string> packagesInstalled;
            auto pkgs = split(package, "\\s+");
            for(auto pkg : pkgs){
                installPrivate(pkg,location,packagesInstalled);
            }
        }
    };
};