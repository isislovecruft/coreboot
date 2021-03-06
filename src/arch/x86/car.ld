/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2006 Advanced Micro Devices, Inc.
 * Copyright (C) 2008-2010 coresystems GmbH
 * Copyright 2015 Google Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* This file is included inside a SECTIONS block */
. = CONFIG_DCACHE_RAM_BASE;
.car.data . (NOLOAD) : {
	/* Vboot work buffer is completely volatile outside of verstage and
	 * romstage. Appropriate code needs to handle the transition. */
#if IS_ENABLED(CONFIG_SEPARATE_VERSTAGE)
	VBOOT2_WORK(., 16K)
#endif
	/* The pre-ram cbmem console as well as the timestamp region are fixed
	 * in size. Therefore place them at the beginning .car.data section
	 * so that multiple stages (romstage and verstage) have a consistent
	 * link address of these shared objects. */
	PRERAM_CBMEM_CONSOLE(., (CONFIG_LATE_CBMEM_INIT ? 0 : 0xc00))
	_car_data_start = .;
	/* The timestamp implementation relies on this storage to be around
	 * after migration. One of the fields indicates not to use it as the
	 * backing store once cbmem comes online. Therefore, this data needs
	 * to reside in the migrated area (between _car_data_start and
	 * _car_data_end). */
#if IS_ENABLED(CONFIG_HAS_PRECBMEM_TIMESTAMP_REGION)
	TIMESTAMP(., 0x100)
#endif
	/* _car_global_start and _car_global_end provide symbols to per-stage
	 * variables that are not shared like the timestamp and the pre-ram
	 * cbmem console. This is useful for clearing this area on a per-stage
	 * basis when more than one stage uses cache-as-ram for CAR_GLOBALs. */
	_car_global_start = .;
	*(.car.global_data);
	. = ALIGN(ARCH_POINTER_ALIGN_SIZE);
	_car_global_end = .;
	_car_data_end = .;
}

/* Global variables are not allowed in romstage
 * This section is checked during stage creation to ensure
 * that there are no global variables present
 */

. = 0xffffff00;
.illegal_globals . : {
	*(EXCLUDE_FILE ("*/libagesa.*.a:" "*/romstage*/buildOpts.o" "*/romstage*/agesawrapper.o" "*/vendorcode/amd/agesa/*" "*/vendorcode/amd/cimx/*") .data)
		*(EXCLUDE_FILE ("*/libagesa.*.a:" "*/romstage*/buildOpts.o" "*/romstage*/agesawrapper.o" "*/vendorcode/amd/agesa/*" "*/vendorcode/amd/cimx/*") .data.*)
	*(.bss)
	*(.bss.*)
	*(.sbss)
	*(.sbss.*)
}

_bogus = ASSERT((CONFIG_DCACHE_RAM_SIZE == 0) || (SIZEOF(.car.data) <= CONFIG_DCACHE_RAM_SIZE), "Cache as RAM area is too full");
