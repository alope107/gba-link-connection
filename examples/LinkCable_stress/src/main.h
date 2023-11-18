#include <tonc.h>
#include "../../../lib/LinkCable.hpp"
#include "../../../lib/LinkUniversal.hpp"

// #define USE_LINK_UNIVERSAL

#ifndef USE_LINK_UNIVERSAL
extern LinkCable* link;
#endif
#ifdef USE_LINK_UNIVERSAL
extern LinkUniversal* link;
#endif