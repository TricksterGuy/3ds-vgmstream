#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <3ds.h>

/// Directory to fetch music files from on sd card.
const std::string music_directory = "/music";

/// Maximum number of samples to get at once
u32 max_samples = 65536;

#endif
