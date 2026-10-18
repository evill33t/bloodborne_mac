// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>
#include <utility>
namespace MacSettings { struct Values; }
namespace MacNetwork {
struct Reply { int status=0; std::string body,error; };
Reply Request(const std::string& url,const std::string* post,int timeout_ms,bool verify_tls,
              const std::vector<std::pair<std::string,std::string>>& headers);
std::string Ruleset();
void Configure(const MacSettings::Values& values);
void Register();
void Initialize();
void SetImage(unsigned char* image,std::uint64_t size);
std::string AccountName();
std::string AccountToken();
void LoadAccount(const std::string& server);
void LinkAccount();
void SignOut();
bool AccountBusy();
std::string AccountStatus();
std::string TakeLinkUrl();
}
