#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct Route {
    std::string sourceIP;
    int sourcePort;
    std::string destDomain;
    int destPort;

    Route(const std::string& sIP, int sPort, const std::string& dDomain, int dPort)
        : sourceIP(sIP), sourcePort(sPort), destDomain(dDomain), destPort(dPort) {}
};

class Config {
public:
    Config(const std::string& configFile);
    ~Config() = default;

    bool loadConfig();
    const std::vector<Route>& getRoutes() const;
    
private:
    std::string m_configFile;
    std::vector<Route> m_routes;
}; 