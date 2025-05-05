/*
 * Copyright (C) 2025 Matej Bölcskei, ETH Zurich
 * Licensed under the GNU General Public License as published by the Free Software Foundation, version 3.
 * See LICENSE or <https://www.gnu.org/licenses/gpl-3.0.html> for details.
 * 
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

int allocate_contiguous_evict_shadow(uintptr_t (*get_pa)(void *), size_t alloc_size, void **return_ptr, uintptr_t *return_pa);

int allocate_contiguous_massage_shadow(uintptr_t (*get_pa)(void *), size_t alloc_size, void **return_ptr, uintptr_t *return_pa, uintptr_t *shadow_hint);