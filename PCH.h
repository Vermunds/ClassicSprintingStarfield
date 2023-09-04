#pragma once

#include <cstdint>
#include <string>

#define DLLEXPORT extern "C" [[maybe_unused]] __declspec(dllexport)
#define SFSEAPI __cdecl

using namespace std::literals;
