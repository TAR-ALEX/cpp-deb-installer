// BSD 3-Clause License

// Copyright (c) 2022, Alex Tarasov
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:

// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.

// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.

// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
#include <Semaphore.h>
#include <map>
#include <set>
#include <sstream>
#include <boost/regex.hpp>
#include <io_tools/ostream_proxy.hpp>
#include <ThreadPool.hpp>


namespace deb{
    namespace {
        using namespace std;
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
            int numRetry = 3;
            for(int i = 1; i <= numRetry; i++){
                try{
                    std::string scheme = "";
                    std::string host = "";
                    std::string path = "";

                    tie(scheme, host, path) = splitUrl(url);

                    httplib::Client cli((scheme+host).c_str());
                    cli.set_follow_location(true);
                    cli.set_read_timeout(5);
                    cli.set_connection_timeout(7);
                    cli.set_write_timeout(3);
                    auto res = cli.Get(path.c_str());
                    if(res.error() != httplib::Error::Success) throw runtime_error("Request error " + url);
                    return res->body;
                }catch(exception& e){
                    if(i == numRetry)
                        throw e;
                }
            }
            throw runtime_error("Failed to fetch url: " + url);
        }

        std::filesystem::path downloadFile(string url, std::filesystem::path location){
            int numRetry = 3;
            for(int i = 1; i <= numRetry; i++){
                try{
                    std::string scheme = "";
                    std::string host = "";
                    std::string path = "";

                    tie(scheme, host, path) = splitUrl(url);

                    std::filesystem::path extractFilename = path;
                    std::filesystem::path filename = extractFilename.filename();

                    fs::create_directories(location);
                    
                    ofstream file(location/filename);

                    httplib::Client cli((scheme+host).c_str());
                    cli.set_read_timeout(20);
                    cli.set_connection_timeout(20);
                    cli.set_write_timeout(20);
                    cli.set_follow_location(true);

                    auto res = cli.Get(path.c_str(), httplib::Headers(),
                    [&](const httplib::Response &response) {
                        return true; // return 'false' if you want to cancel the request.
                    },
                    [&](const char *data, size_t data_length) {
                        file.write(data, data_length);
                        return true; // return 'false' if you want to cancel the request.
                    });

                    file.close();
                    if(res.error() != httplib::Error::Success) throw runtime_error("Request error " + url);
                    return location/filename;
                }catch(exception& e){
                    if(i == numRetry)
                        throw e;
                }
            }
            throw runtime_error("Failed to fetch url: " + url);
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
        io_tools::ostream_proxy cout{{&std::cout}};
        set<string> sourcesList;
        map<string,string> packageToUrl;
        set<string> installed;
        mutex installLock;
        ThreadPool trm{16};

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

                while(ss >> component){
                    result.insert({
                        baseUrl, 
                        baseUrl+"/dists/"+distribution+"/"+component+
                        "/"+architecture+"/"+"Packages.gz"
                    });
                }
                
            }
            for(const auto& elem : result)
                cout << get<1>(elem) << "\n";
            return result;
        }
        void getPackageList(){
            auto urls = getListUrls();
            for(const auto& entry : urls){
                trm.schedule([=](){
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
                                cout << "Match not found\n";
                                continue;
                            }
                        }
                        {
                            static boost::regex rgx("Filename:\\s?([^\\r\\n]*)", boost::regex::optimize);
                            boost::smatch matches;
                            if(boost::regex_search(entry, matches, rgx) && matches.size() == 2) {
                                packagePath = matches[1];
                            } else {
                                cout << "Match not found\n";
                                continue;
                            }
                        }
                        packagePath=baseUrl+"/"+packagePath;
                        auto provides = getFields(entry, "Provides");
                        auto source = getFields(entry, "Source");
                        provides.insert(source.begin(), source.end());
                        provides.insert(packageName);
                        {
                            std::unique_lock l(installLock);
                            for(auto i : provides){
                                if(!packageToUrl.count(i)){
                                    packageToUrl[i] = packagePath;
                                }
                            }
                        }
                    }
                });
            }
            trm.wait();
            // for(const auto& elem : packageToUrl){
            //     cout << elem.first << " -> " << elem.second << "\n";
            // }
        }
        set<string> getFields(const string& contolFile, string typeOfDep = "Depends"){
            set<string> result;
            map<string, boost::regex> rgx;
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
            boost::regex r("(?:\\s+)|(?:\\(.*\\))|(?::.*)", boost::regex::optimize);
            for(auto entry : entries){
                // (\S+)\s?(?:\((\S*)\s?(\S*)\))?
                result.insert(boost::regex_replace(entry, r, ""));
            }
            return result;
        }
        void installPrivate(string package, string location){
            string url;
            vector<std::thread> threads;
            {
                unique_lock<mutex> lock(installLock);

                if(!packageToUrl.count(package)) throw runtime_error("package "+package+" does not exist in repository.");
                url = packageToUrl[package];
                
                if(installed.count(url)){
                    cout << "already installed " << package << endl;
                    return;
                }            

                installed.insert(url);
                cout << "installed " << package << "\n";
            }


            auto packageLoc = downloadFile(url, "./tmp");
            ar::Reader deb(packageLoc.string());
            deb.allowSeekg = true;
            auto versionStream = deb.getFileStream("debian-binary");
            string version = streamToString(versionStream);
            if(version.find("2.0") == string::npos) throw runtime_error("package "+package+" has a bad version number "+version+".");
            string controlString;

            try{
                deb.reset();
                auto dataTarCompressedStream = deb.getFileStream("data.tar.xz");
                bxz::istream dataTarStream(dataTarCompressedStream);
                tar::Reader dataTar(dataTarStream);
                dataTar.throwOnUnsupported = false;
                dataTar.linksAreCopies = false;
                dataTar.extractAll(location);
            }catch(...){
                deb.reset();
                auto dataTarCompressedStream = deb.getFileStream("data.tar.gz");
                bxz::istream dataTarStream(dataTarCompressedStream);
                tar::Reader dataTar(dataTarStream);
                dataTar.throwOnUnsupported = false;
                dataTar.linksAreCopies = false;
                dataTar.extractAll(location);
            }

            if(!recursive) return;
            
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
                trm.schedule([this, dep, location](){
                    this->installPrivate(dep, location);
                });
            }
        }
    public:
        string architecture = "binary-amd64";
        bool recursive = true;
        bool throwOnFailedDependency = false;

        Installer(set<string> l) : sourcesList(l) {}
        void install(string package, string location){
            if(packageToUrl.empty()) getPackageList();
            set<string> packagesInstalled;
            auto pkgs = split(package, "\\s+");
            for(auto pkg : pkgs){
                trm.schedule([this, pkg, location](){
                    installPrivate(pkg,location);
                });
            }
            trm.forwardExceptions = throwOnFailedDependency;
            trm.wait();
        }
    };
};