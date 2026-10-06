#ifndef MAPS_HPP
#define MAPS_HPP

#include <string>
#include <sys/types.h>
#include <vector>

namespace runet
{
namespace maps
{

#define MAP_HAS_PERMISSION(m, p) (((m)->permission & (p)) == (p))

#define PERMISSION_READ 0x01
#define PERMISSION_WRITE 0x02
#define PERMISSION_EXECUTE 0x04
#define PERMISSION_PRIVATE 0x08
#define PERMISSION_SHARED 0x10

union device_t {
    struct {
        uint8_t major, minor;
    };

    uint16_t device_number;
};

struct pathname_t {
    bool special;
    bool anonymous;
    std::string name;
};

struct Mapping {
    uintptr_t start_address;
    uintptr_t end_address;
    uint8_t permission;
    uint32_t offset;
    device_t dev;
    ino_t inode;
    pathname_t path;
};

std::vector<Mapping> GetCurrentProcessMappings();

Mapping ParseMappingFromLine(std::string line);

}; // namespace maps;
}; // namespace runet;

#endif /** MAPS_HPP */
