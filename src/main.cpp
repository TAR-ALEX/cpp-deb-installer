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

#include <deb/deb-downloader.hpp>

using namespace std;
namespace fs = std::filesystem;



int main() {
	deb::Installer inst(
		{"deb http://packages.linuxmint.com vera main upstream import backport",
		 "deb http://archive.ubuntu.com/ubuntu jammy main restricted universe multiverse",
		 "deb http://archive.ubuntu.com/ubuntu jammy-updates main restricted universe multiverse",
		 "deb http://archive.ubuntu.com/ubuntu jammy-backports main restricted universe multiverse",
		 "deb http://security.ubuntu.com/ubuntu/ jammy-security main restricted universe multiverse",
		 "deb http://archive.canonical.com/ubuntu/ jammy partner",
		 "deb http://ftp.us.debian.org/debian buster main "}
	);
	inst.recursionLimit = 3;
	inst.throwOnFailedDependency = true;
	inst.install("qtbase5-dev qtchooser qt5-qmake qtbase5-dev-tools", "../deb/qt");
	//inst.install("libboost-all-dev", "../deb/boost");
	inst.install(
		"libboost-all-dev",
		{
			{"./usr/lib/x86_64-linux-gnu", "../deb/boost-lib"},
			{"./usr/include", "../deb/boost-include"},
		}
	);

	return 0;
}