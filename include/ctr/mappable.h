/**
 * @file mappable.h
 * @brief Mappable memory allocator.
 */
#pragma once

#include <ctr/types.h>

#define OS_MAP_AREA_BEGIN  0x10000000 ///< Start of the mappable area in the virtual address space
#define OS_MAP_AREA_END    0x14000000 ///< End of the mappable area in the virtual address space

/**
 * @brief Initializes the mappable allocator.
 * @param addrMin Minimum address.
 * @param addrMax Maxium address.
 */
void mappableInit(u32 addrMin, u32 addrMax);

/**
 * @brief Finds a mappable memory area.
 * @param size Size of the area to find.
 * @return The mappable area.
 */
void* mappableAlloc(u32 size);

/**
 * @brief Frees a mappable area (stubbed).
 * @param mem Mappable area to free.
 */
void mappableFree(void* mem);
