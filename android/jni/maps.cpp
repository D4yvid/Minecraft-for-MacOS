#include "maps.hpp"
#include <ctype.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <exception>
#include <memory>
#include <string>
#include <vector>
#include "log.hpp"

using namespace runet::maps;

static uint8_t asPermission(const char *str) {
    uint8_t perm = 0x00;
    
    if (*str == 'r')
        perm |= PERMISSION_READ;

    if (*(str + 1) == 'w')
        perm |= PERMISSION_WRITE;

    if (*(str + 2) == 'x')
        perm |= PERMISSION_EXECUTE;

    if (*(str + 3) == 's')
        perm |= PERMISSION_SHARED;
    else if (*(str + 3) == 'p')
        perm |= PERMISSION_PRIVATE;

    return perm;
}

// START ADDR   END ADDR     PERM OFFSET   DEV   INODE                      PATHNAME
// 55b066aa2000-55b066aa4000 r--p 00000000 08:20 1312                       /usr/bin/cat
runet::maps::Mapping runet::maps::ParseMappingFromLine(std::string line) {
    Mapping map = {};

    map.start_address = strtoull(strtok((char *) line.c_str(), "-"), NULL, 16);
    map.end_address   = strtoull(strtok(NULL, " "), NULL, 16);
    map.permission    = asPermission(strtok(NULL, " "));
    map.offset        = strtoull(strtok(NULL, " "), NULL, 16);
    map.dev.major     = atoi(strtok(NULL, ":"));
    map.dev.minor     = atoi(strtok(NULL, " "));
    map.inode         = atoi(strtok(NULL, " "));

    char *str = strtok(NULL, " \n");

    if (str != nullptr) {
        map.path.name = std::string(str);
        map.path.special   = map.path.name[0] == '[';
    } else {
        map.path.anonymous = true;
    }

    return map;
}

std::vector<runet::maps::Mapping> runet::maps::GetCurrentProcessMappings() {
    std::vector<runet::maps::Mapping> lMappings;

    std::ifstream lFile("/proc/self/maps");

    if (!lFile.is_open()) {
        return std::vector<Mapping>();
    }

    std::string lLine;
    while (std::getline(lFile, lLine)) {
        Mapping map = ParseMappingFromLine(lLine);
        lMappings.emplace_back(map);
    }

    return lMappings;
}
