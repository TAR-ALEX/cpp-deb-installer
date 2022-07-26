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

#include <vector>
#include <memory>
#include <iostream>
#include <map>
#include <vector>
#include <functional>
#include <filesystem>
#include <fstream>
#include <io_tools/isubstream.hpp>

namespace tar{
	namespace   // anonymous namespace
    {
		#pragma pack(0)
		struct posix_header
		{                              		 /* byte offset */
			uint8_t name[100];               /*   0 */
			uint8_t mode[8];                 /* 100 */
			uint8_t uid[8];                  /* 108 */
			uint8_t gid[8];                  /* 116 */
			uint8_t size[12];                /* 124 */
			uint8_t mtime[12];               /* 136 */
			uint8_t chksum[8];               /* 148 */
			uint8_t typeflag;                /* 156 */
			uint8_t linkname[100];           /* 157 */
			uint8_t magic[6];                /* 257 */
			uint8_t version[2];              /* 263 */
			uint8_t uname[32];               /* 265 */
			uint8_t gname[32];               /* 297 */
			uint8_t devmajor[8];             /* 329 */
			uint8_t devminor[8];             /* 337 */
			uint8_t prefix[155];             /* 345 */								 	 
			uint8_t padding[12];			 /* 500 */
											 /* 512 */
		};

		struct parsed_posix_header
		{                              		
			std::string name;             
			uint8_t mode;              
			uint64_t uid;              
			uint64_t gid;                  
			uint64_t size;              
			uint64_t mtime;              
			std::string chksum;             
			uint8_t typeflag;              
			std::string linkname;         
			std::string magic;              
			std::string version;         
			std::string uname;              
			std::string gname;            
			std::string devmajor;             
			std::string devminor;             
			std::string prefix;             								 	 
											
		};
	}

    class Reader{
	private:

		std::string replacePrefix(std::string str, const std::string& from, const std::string& to) {
			size_t start_pos = 0;
			if((start_pos = str.rfind(from, 0)) != std::string::npos) {
				str.replace(start_pos, from.length(), to);
			}
			return str;
		}

		std::pair<bool,std::filesystem::path> changeRoot(std::filesystem::path path, std::filesystem::path from, std::filesystem::path to){
			path = ("."/path).lexically_normal();
			from = ("."/from).lexically_normal();
			to = ("."/to).lexically_normal();

			if(from == "" || from == "." || from == "./"){
				return {true, (to/path).lexically_normal()};
			}
			
			bool fromIsDir = !from.has_filename();
			bool toIsDir = !to.has_filename();
			bool pathIsDir = !path.has_filename();

			to = (to/"").lexically_normal();
			from = (from/"").lexically_normal();
			path = (path/"").lexically_normal();

			if(path.string().rfind(from,0) != 0){
				return {false, path};
			}

			std::filesystem::path result;
			if(toIsDir && !fromIsDir){//from is a file && to is a dir
				to = to.replace_filename(from.parent_path().filename());
				to = (to/"").lexically_normal();
			}

			// case: from is a dir && to is a file
			//     dir cannot me moved to a file, 
			//     must also be a dir (same as two dirs)
			// case: two dirs

			// do nothing in this case

			if(from == "" || from == "." || from == "./")
				result = to/path;
			else
				result = replacePrefix(path, from, to);
			
			result = result.lexically_normal();

			if(!pathIsDir) result = result.parent_path().lexically_normal();

			// std::cout << fromIsDir << " " << toIsDir << " ";
			// std::cout << " path: " << path.string();
			// std::cout << " from: " << from.string();
			// std::cout << " to: " << to.string();
			// std::cout << " result: " << result.string() << "\n";
			return {true, result};
		}


		posix_header unpackPosixHeader(const std::array< char, 512 > raw){
			posix_header header;
			header = *((posix_header*)raw.data());
			return header;
		}

		std::array< char, 512 > packPosixHeader(const posix_header& header){
			std::array< char, 512 > raw;
			char* headerAsCharArray = ((char*)(&header));
			std::copy(headerAsCharArray, headerAsCharArray+sizeof(posix_header), raw.data());
			return raw;
		}

		inline std::string calc_checksum(posix_header header){
			uint8_t* headerAsCharArray = ((uint8_t*)(&header));
			for(uint64_t i = 0; i < sizeof(header.chksum); i++) header.chksum[i] = ' ';
			
			unsigned long long sum = 0;
			for(uint64_t i = 0; i < sizeof(posix_header); i++) sum += headerAsCharArray[i];

			std::ostringstream os;
			os << std::oct << std::setfill('0') << std::setw(6) << sum << '\0' << ' ';

			return os.str();
		}

		inline parsed_posix_header parsePosixHeader(
			std::array< char, 512 > const& buffer
		){
			auto header = unpackPosixHeader(buffer);
			parsed_posix_header result;
			result.chksum = std::string((char*)header.chksum, sizeof(header.chksum));
			result.magic = std::string((char*)header.magic, sizeof(header.magic));
			result.size = static_cast< std::size_t >(std::stol(std::string((char*)header.size, sizeof(header.size)), 0, 8));
			result.name = std::string((char*)header.name, sizeof(header.name));
			result.name = std::string(result.name.c_str());
			result.linkname = std::string((char*)header.linkname, sizeof(header.linkname));
			result.linkname = std::string(result.linkname.c_str());
			result.typeflag = header.typeflag;

			if(result.magic == "ustar"){
				throw std::runtime_error(
					"Tar: loaded file without magic 'ustar', magic is: '"
					+ std::string(result.magic) + "'"
				);
			}

			if(result.chksum != calc_checksum(header)){
				throw std::runtime_error(
					std::string("Tar: loaded file with wrong checksum '") + std::string(result.chksum) + std::string("' != '") + std::string(calc_checksum(header) + "'")
				);
			}

			return result;
		}

		std::unique_ptr< std::ifstream > inputStreamPtr;
		std::istream& inputStream;
		std::map< std::string, std::size_t > files;
    public:
		bool throwOnUnsupported = true;
		bool linksAreCopies = false;
		bool allowSeekg = false;

        Reader(std::string const& filename):
			inputStreamPtr(std::make_unique< std::ifstream >(
				filename, std::ios_base::in | std::ios_base::binary
			)),
			inputStream(*inputStreamPtr.get())
		{}

		Reader(std::istream& is):
			inputStream(is)
		{}
	    
	    
		void reset(){
			inputStream.clear(); 
			inputStream.seekg(0, std::ios::beg);
			inputStream.clear(); 
		}


		io_tools::isubstream getFileStream(std::filesystem::path source){
			std::array< char, 512 > buffer;
			std::array< char, 4096 > dataBuffer;
			while(inputStream){
				inputStream.read(buffer.data(), 512);

				if(!inputStream) break;
				
				parsed_posix_header header;
				header = parsePosixHeader(buffer);
				
				if(header.name == "././@LongLink"){
					std::string longname = "";
					longname.resize(header.size);
					if(longname.length() != header.size){
						throw std::runtime_error("Could not allocate a string of size " + std::to_string(header.size));
					}
					if(!inputStream.read(longname.data(), header.size)){
						throw std::runtime_error("Failed to read longname of size " + std::to_string(header.size));
					}

					int nameBlockOffset = (512 - (header.size % 512)) % 512;
					inputStream.read(buffer.data(), nameBlockOffset);
					inputStream.read(buffer.data(), 512);
					if(!inputStream) break;
					header = parsePosixHeader(buffer);
					header.name = longname;
				}

				std::streampos dataBlockOffset = (512 - (header.size % 512)) % 512;

				std::filesystem::path inTarPath = header.name;
				source = source.lexically_normal();
				inTarPath = inTarPath.lexically_normal();
				
				if(source != inTarPath){
					if(allowSeekg){
						std::streampos pos = header.size + dataBlockOffset;
						inputStream.seekg(inputStream.tellg() + pos);
					}else{
						for(std::size_t i = header.size + dataBlockOffset; i > 0;){	
							int howMuchToRead = (dataBuffer.size() < i) ? (dataBuffer.size()) : (i);
							inputStream.read(dataBuffer.data(), howMuchToRead);
							i -= howMuchToRead;
						}
					}
				} else if(header.typeflag == '5'){ // is dir
					throw std::runtime_error("File is a directory " + std::to_string(header.size));
				}else if(header.typeflag == '0'){ // is file
					return io_tools::isubstream(inputStream.rdbuf(), inputStream.tellg(), header.size);
				}else{
					if(throwOnUnsupported){
						throw std::runtime_error(
							"Tar has an unsuppoted entry type (TODO - not implemented): " + header.name
						);
					}
				}

				if(!inputStream){
					throw std::runtime_error(
						"Tar filename-entry with illegal size: " + header.name
					);
				}
			}
			throw std::runtime_error(
				"File cannot be found in tar: " + source.string()
			);
		}



		void extractAll(std::filesystem::path destination){
			extractPath("./", destination);
		}

        void extractPath(std::filesystem::path source, std::filesystem::path destination){
			std::vector<std::function<void()>> toDo;
			std::shared_ptr<void> _ (nullptr, [&](...){ 
				inputStream.clear(); 
				inputStream.seekg(0, std::ios::beg);
				inputStream.clear(); 
				for(auto& f : toDo) f();
			});

			files.clear();
			static constexpr std::array< char, 512 > empty_buffer{};

			std::array< char, 512 > buffer;
			std::array< char, 4096 > dataBuffer;
			while(inputStream){
				inputStream.read(buffer.data(), 512);

				if(buffer == empty_buffer){
					inputStream.read(buffer.data(), 512);
					if(buffer != empty_buffer || !inputStream){
						throw std::runtime_error("Corrupt tar-file.");
					}
					break;
				}

				parsed_posix_header header = parsePosixHeader(buffer);

				if(header.name == "././@LongLink"){
					std::string longname = "";
					longname.resize(header.size);

					if(longname.length() != header.size){
						throw std::runtime_error("Could not allocate a string of size " + std::to_string(header.size));
					}
					if(!inputStream.read(longname.data(), header.size)){
						throw std::runtime_error("Failed to read longname of size " + std::to_string(header.size));
					}

					int nameBlockOffset = (512 - (header.size % 512)) % 512;
					inputStream.read(buffer.data(), nameBlockOffset);
					inputStream.read(buffer.data(), 512);
					header = parsePosixHeader(buffer);
					header.name = longname;
				}
				
				if(files.count(header.name)){
					throw std::runtime_error(
						"Duplicate filename-entry while reading tar-file: " +
						header.name
					);
				}

				files[header.name] = 0;

				std::streampos dataBlockOffset = (512 - (header.size % 512)) % 512;

				std::filesystem::path path;
				bool isValid;
				std::tie(isValid, path) = changeRoot(header.name, source, destination);

				// std::cout << "header.name = " << header.name << std::endl;
				// std::cout << "source = " << source << std::endl;
				// std::cout << "destination = " << destination << std::endl;
				// std::cout << "path = " << path << std::endl << std::endl;
				
				if(!isValid){
					for(std::size_t i = header.size + dataBlockOffset; i > 0;){
						int howMuchToRead = (dataBuffer.size() < i) ? (dataBuffer.size()) : (i);
						inputStream.read(dataBuffer.data(), howMuchToRead);
						i -= howMuchToRead;
					}
				} else if(header.typeflag == '5'){ // is dir
					std::filesystem::create_directories(path);
					for(std::size_t i = header.size + dataBlockOffset; i > 0;){
						int howMuchToRead = (dataBuffer.size() < i) ? (dataBuffer.size()) : (i);
						inputStream.read(dataBuffer.data(), howMuchToRead);
						i -= howMuchToRead;
					}
				}else if(header.typeflag == '0' || header.typeflag == '\0'){ // is file
					if(!path.has_filename()) path.replace_filename(source.filename());

					
					std::filesystem::create_directories(path.parent_path());
					std::ofstream f = std::ofstream(path, std::ios::out | std::ios::binary);
					
					for(std::size_t i = header.size; i > 0;){
						int howMuchToRead = (dataBuffer.size() < i) ? (dataBuffer.size()) : (i);
						inputStream.read(dataBuffer.data(), howMuchToRead);
						f.write(dataBuffer.data(), howMuchToRead);
						i -= howMuchToRead;
					}
					
					f.close();

					for(std::size_t i = dataBlockOffset; i > 0;){
						int howMuchToRead = (dataBuffer.size() < i) ? (dataBuffer.size()) : (i);
						inputStream.read(dataBuffer.data(), howMuchToRead);
						i -= howMuchToRead;
					}
				}else if(header.typeflag == '2'){ // is softlink
					std::string linkname = header.linkname;
					toDo.emplace_back([=](){
						try{
							if(linksAreCopies){
								//std::cout << "softlink " << linkname << "\ncopy " << (path.parent_path()/linkname).lexically_normal() << "\nto " << path << "\n\n";
								std::filesystem::copy(
									(path.parent_path()/linkname).lexically_normal(), 
									path, 
									std::filesystem::copy_options::overwrite_existing | 
									std::filesystem::copy_options::recursive
								);
							}else{
								std::filesystem::create_symlink(linkname, path);
							}
						}catch(...){}
					});
					
				}else if(header.typeflag == '1'){ // is hardlink
					std::string linkname = header.linkname;
					toDo.emplace_back([=](){
						try{
							std::filesystem::path localPath = linkname;
							bool isValid;
							std::tie(isValid, localPath) = changeRoot(localPath, source, destination);

							if(linksAreCopies){
								//std::cout << "hardlink " << linkname << "\ncopy " << localPath << "\nto " << path << "\n\n";
								std::filesystem::copy(
									localPath.lexically_normal(), 
									path, 
									std::filesystem::copy_options::overwrite_existing | 
									std::filesystem::copy_options::recursive
								);
							}else{
								std::filesystem::create_hard_link(localPath, path);
							}
						}catch(...){}
					});
				}else{
					std::cout << "file = " << header.name << "  <----->  type = " << (char)header.typeflag << std::endl;
					if(throwOnUnsupported){
						throw std::runtime_error(
							"Tar has an unsuppoted entry type (TODO - not implemented): " + header.name
						);
					}
				}
				

				if(!inputStream){
					throw std::runtime_error(
						"Tar filename-entry with illegal size: " + header.name
					);
				}
			}
        }
        
    };
};
