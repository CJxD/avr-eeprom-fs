/*
 eeprom-fs: a micro EEPROM filesystem
 
 Copyright (c) 2014 Chris Watts (cw17g12@ecs.soton.ac.uk)

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 ==========================================================================

 eeprom-fs is a dynamic wear levelling filesystem that stores data in files
 marked by integers.

 The architecture is based off FAT with the file allocation located statically
 towards the start of the EEPROM memory. Because of this, the FAT table can be subject
 to wearing over several tens of thousands of file writes.
 */

#include <avr/eeprom.h>
#include <avr/io.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "eeprom-fs.h"

#define NULL_PTR -1

void* get_block_pointer(lba_t block);
lba_t last_block_in_chain(lba_t block);
lba_t write_block_data(fdata_t* data);
void link(file_handle_t* fh);
void unlink(lba_t block);
void relink(lba_t block, lba_t target);

/**
 * Debugging
 */
uint8_t __debug = 0;

void _fs_error(const char *format, ...);
void _fs_debug1(const char *format, ...);
void _fs_debug2(const char *format, ...);
void _fs_debug3(const char *format, ...);
void _fs_debug4(const char *format, ...);

/*
 * Cached allocation table
 */
file_alloc_t alloc_table[EEPROM_FS_MAX_FILES + 1];
// Shortcut to next free block
lba_t* const next_free_block = &alloc_table[EEPROM_FS_MAX_FILES].data_block;

/**
 * Initialise the file system
 */
void init_eepromfs()
{
	_fs_debug1("Initialising filesystem.\n");

	// Retrieve metadata
	_fs_debug2("Loading metadata...");
	fs_meta_t stored_meta;
	eeprom_read_block((void*) &stored_meta,
			(void*) (EEPROM_FS_START + EEPROM_FS_META_OFFSET),
			sizeof(fs_meta_t));
	_fs_debug2("Done.\n");

	// Format if metadata has changed
	if (stored_meta.block_size != EEPROM_FS_BLOCK_SIZE
			|| stored_meta.start_address != EEPROM_FS_START
			|| stored_meta.fs_size != EEPROM_FS_SIZE
			|| stored_meta.max_files != EEPROM_FS_MAX_FILES
			|| stored_meta.max_blocks_per_file != EEPROM_FS_MAX_BLOCKS_PER_FILE)
	{
		format_eepromfs(FORMAT_QUICK);
	}

	// Load allocation table
	_fs_debug2("Loading file allocation table...");
	eeprom_read_block(alloc_table,
			(void*) (EEPROM_FS_START + EEPROM_FS_ALLOC_TABLE_OFFSET),
			sizeof(alloc_table));
	_fs_debug2("Done.\n");

	_fs_debug3("Next free block: %d\n", *next_free_block);

	_fs_debug1("Filesystem initialised.\n");
}

/**
 * Perform a format of the EEPROM
 *
 * \param f FORMAT_FULL clears all the data in each block.
 *          FORMAT_QUICK resets the allocation table and
 *          	free block chain pointers only.
 *          FORMAT_WIPE performs a full wipe of the EEPROM
 *          	before performing a quick format
 */
void format_eepromfs(format_type_t f)
{
	_fs_debug1("Formatting filesystem.\n");

	if (f == FORMAT_WIPE)
	{
		wipe_eeprom();
	}

	/*
	 * Mark all blocks as free
	 */
	block_t block;
	if (f == FORMAT_FULL)
	{
		// Clear block data
		for (size_t i = 0; i < EEPROM_FS_BLOCK_DATA_SIZE; i++)
		{
			block.data[i] = 0;
		}
	}

	for (lba_t i = 0; i < EEPROM_FS_NUM_BLOCKS; i++)
	{
		block.next_block = i - 1;
		if (f == FORMAT_FULL)
		{
			// Overwrite entire block, clearing data
			_fs_debug3("Relinking block %d -> %d...", i, block.next_block);

			eeprom_update_block((void*) &block, get_block_pointer(i),
			EEPROM_FS_BLOCK_SIZE);

			_fs_debug3("Done.\n");
		}
		else
		{
			// Relink address only
			relink(i, block.next_block);
		}
	}

	/*
	 * Allocation table
	 */
	_fs_debug2("Writing file allocation table...");

	// Set all allocations to 'null'
	file_alloc_t null;
	null.data_block = NULL_PTR;
	null.filesize = 0;
	for (uint16_t i = 0; i < EEPROM_FS_MAX_FILES; i++)
	{
		alloc_table[i] = null;
	}

	// Set free block at end of allocation table
	file_alloc_t free;
	free.data_block = EEPROM_FS_NUM_BLOCKS - 1;
	free.filesize = 0;
	alloc_table[EEPROM_FS_MAX_FILES] = free;

	eeprom_update_block((void*) alloc_table,
			(void*) (EEPROM_FS_START + EEPROM_FS_ALLOC_TABLE_OFFSET),
			sizeof(alloc_table));

	_fs_debug2("Done.\n");

	// Write meta
	_fs_debug2("Writing metadata...");
	fs_meta_t this_meta;
	this_meta.block_size = EEPROM_FS_BLOCK_SIZE;
	this_meta.start_address = EEPROM_FS_START;
	this_meta.fs_size = EEPROM_FS_SIZE;
	this_meta.max_files = EEPROM_FS_MAX_FILES;
	this_meta.max_blocks_per_file = EEPROM_FS_MAX_BLOCKS_PER_FILE;
	eeprom_write_block((void*) &this_meta,
			(void*) (EEPROM_FS_START + EEPROM_FS_META_OFFSET),
			sizeof(fs_meta_t));
	_fs_debug2("Done.\n");

	_fs_debug1("Successfully formatted.\n");
}

file_handle_t open_for_write(fname_t filename)
{
	_fs_debug1("Preparing file %d for writing.\n", filename);

	// Wrap filename around in case it's larger than maximum supported
	if (filename > EEPROM_FS_MAX_FILES)
	{
		filename = filename % EEPROM_FS_MAX_FILES;
		_fs_debug2("Filename too large - truncated to %d.\n", filename);
	}

	file_handle_t fh;
	fh.filename = filename;
	fh.filesize = 0;
	fh.type = FH_WRITE;
	fh.first_block = NULL_PTR;
	fh.last_block = NULL_PTR;

	_fs_debug1("File ready.\n");

	return fh;
}

file_handle_t open_for_append(fname_t filename)
{
	_fs_debug1("Preparing file %d for appending.\n", filename);

	// Wrap filename around in case it's larger than maximum supported
	if (filename > EEPROM_FS_MAX_FILES)
	{
		filename = filename % EEPROM_FS_MAX_FILES;
		_fs_debug2("Filename too large - truncated to %d.\n", filename);
	}

	file_handle_t fh;
	fh.filename = filename;
	fh.filesize = alloc_table[filename].filesize;
	fh.type = FH_APPEND;
	fh.first_block = NULL_PTR;
	fh.last_block = NULL_PTR;

	_fs_debug1("File ready.\n");

	return fh;
}

file_handle_t open_for_read(fname_t filename)
{
	_fs_debug1("Preparing file %d for reading.\n", filename);

	// Wrap filename around in case it's larger than maximum supported
	if (filename > EEPROM_FS_MAX_FILES)
	{
		filename = filename % EEPROM_FS_MAX_FILES;
		_fs_debug2("Filename too large - truncated to %d.\n", filename);
	}

	file_handle_t fh;
	fh.filename = filename;
	fh.filesize = alloc_table[filename].filesize;
	fh.type = FH_READ;
	fh.first_block = alloc_table[filename].data_block;
	fh.last_block = NULL_PTR;

	if (fh.first_block == NULL_PTR)
	{
		_fs_error("File %d not found.\n", filename);
	}
	else
	{
		_fs_debug1("File ready.\n");
	}

	return fh;
}

/**
 * Link a written file to the allocation table and unlink any free space used.
 *
 * File is linked to the allocation table before unlinking free space so if the
 * system fails before the free space is unlinked, there will be no orphaned blocks.
 *
 * \param fh File handle
 */
void close(file_handle_t* fh)
{
	_fs_debug1("Finalising file %d.\n", fh->filename);

	if (fh->type == FH_APPEND)
	{
		// Sum the filesize of the existing file
		fh->filesize += alloc_table[fh->filename].filesize;

		if (alloc_table[fh->filename].filesize > EEPROM_FS_BLOCK_DATA_SIZE)
		{
			// If the first block hasn't been rewritten, link the appended data to the last block
			lba_t last = last_block_in_chain(
					alloc_table[fh->filename].data_block);

			_fs_debug2("Appending block %d to block %d...\n", fh->first_block,
					last);

			// Point the last block of the current file to the first block in the new chain
			relink(last, fh->first_block);

			_fs_debug2("Done.\n");
		}
		else
		{
			// Otherwise, just link the file to the new stuff and discard the old block
			if (alloc_table[fh->filename].filesize > 0)
			{
				unlink(alloc_table[fh->filename].data_block);
			}
			link(fh);
		}
	}
	else
	{
		link(fh);
	}

	_fs_debug2("Marking end of file %d.\n", fh->filename);

	// Mark end of file
	relink(fh->last_block, NULL_PTR);

	_fs_debug1("File %d successfully finalised.\n", fh->filename);
}

/**
 * Write a file into a buffer.
 *
 * \param fh File handle opened for writing or appending
 * \param data Data to write.
 * \param size Amount of data to write (becomes filesize)
 */
void write(file_handle_t* fh, const fdata_t* data, size_t size)
{
	if (fh->type == FH_WRITE || fh->type == FH_APPEND)
	{
		/*
		 * Handle non-complete blocks for appending
		 */
		if (fh->type == FH_APPEND
				&& fh->filesize % EEPROM_FS_BLOCK_DATA_SIZE > 0)
		{
			// Last block of current file is incomplete. Prepend it to the new data.
			size_t overflow = fh->filesize % EEPROM_FS_BLOCK_DATA_SIZE;
			fdata_t* to_write[overflow + size];

			// Read from last block
			file_handle_t last_block_fh = *fh;
			last_block_fh.first_block = last_block_in_chain(
					alloc_table[fh->filename].data_block);
			last_block_fh.filesize = overflow;
			read(&last_block_fh, (fdata_t*) to_write);

			// Copy new data in to new array
			uintptr_t offset = (uintptr_t) to_write
					+ (uintptr_t) (overflow * sizeof(fdata_t));
			memcpy((void*) offset, data, size * sizeof(fdata_t));

			// Update existing variables
			data = (const fdata_t*) to_write;
			size = overflow + size;
		}

		_fs_debug1("Writing %d bytes to file %d.\n", size, fh->filename);

		size_t num_blocks;

		// Don't allow any files bigger than max blocks
		size_t blocks_in_use = alloc_table[fh->filename].filesize
				/ EEPROM_FS_BLOCK_DATA_SIZE;
		if (blocks_in_use + size / EEPROM_FS_BLOCK_DATA_SIZE
				> EEPROM_FS_MAX_BLOCKS_PER_FILE)
		{
			num_blocks = EEPROM_FS_MAX_BLOCKS_PER_FILE - blocks_in_use;
			_fs_error("File too large - write truncated to %d bytes.\n",
					num_blocks * EEPROM_FS_BLOCK_DATA_SIZE);
		}
		else
		{
			num_blocks = (size / EEPROM_FS_BLOCK_DATA_SIZE) + 1;
		}

		if (num_blocks > 0)
		{
			/*
			 * Split data into blocks
			 */
			block_t block;
			size_t num_bytes = EEPROM_FS_BLOCK_DATA_SIZE;

			fh->first_block = *next_free_block;
			for (uint16_t i = 0; i < num_blocks; i++)
			{
				// Don't write more than file's size
				if ((i + 1) * EEPROM_FS_BLOCK_DATA_SIZE > size)
				{
					num_bytes = size % EEPROM_FS_BLOCK_DATA_SIZE;
				}

				// Copy data for this block
				for (uint16_t j = 0; j < num_bytes; j++)
				{
					block.data[j] = data[i * EEPROM_FS_BLOCK_DATA_SIZE + j];
					_fs_debug4("data[%d] = %c\n", i * EEPROM_FS_BLOCK_DATA_SIZE + j,
							block.data[j]);
				}

				// Update file handle data
				fh->last_block = write_block_data(block.data);
			}

			// In case the data was truncated, recalculate size
			if (size > num_blocks * EEPROM_FS_BLOCK_DATA_SIZE)
			{
				fh->filesize = num_blocks * EEPROM_FS_BLOCK_DATA_SIZE;
			}
			else
			{
				fh->filesize = size;
			}

			_fs_debug1("File %d successfully written.\n", fh->filename);
		}
		else
		{
			_fs_error("No more space available for file %d.\n", fh->filename);
		}
	}
	else
	{
		_fs_error("Tried to write to read-only file handle '%d'\n", fh->filename);
	}
}

/**
 * Read a file into a buffer.
 *
 * \param fh File handle opened for reading
 * \param buf Buffer to copy file contents to.
 * 		      Must be large enough to contain entire file contents.
 */
void read(file_handle_t* fh, fdata_t* buf)
{
	if (fh->first_block >= 0 && fh->first_block < (lba_t) EEPROM_FS_NUM_BLOCKS)
	{
		block_t block;
		block.next_block = fh->first_block;

		uint16_t i = 0;
		size_t num_bytes = EEPROM_FS_BLOCK_DATA_SIZE;
		do
		{
			_fs_debug3("Reading from block %d...", block.next_block);
			eeprom_read_block((void*) &block,
					get_block_pointer(block.next_block),
					EEPROM_FS_BLOCK_SIZE);
			_fs_debug3("Done.\n", block.next_block);

			// Don't read more than file's size
			if ((i + 1) * EEPROM_FS_BLOCK_DATA_SIZE > fh->filesize)
			{
				num_bytes = fh->filesize % EEPROM_FS_BLOCK_DATA_SIZE;
			}

			// Copy this block's data to the buffer
			for (uint16_t j = 0; j < num_bytes; j++)
			{
				_fs_debug4("buf[%d] = %c\n", i * EEPROM_FS_BLOCK_DATA_SIZE + j,
						block.data[j]);
				buf[i * EEPROM_FS_BLOCK_DATA_SIZE + j] = block.data[j];
			}
			i++;
		} while (block.next_block != NULL_PTR);
	}
	else
	{
		_fs_error("Tried to read from null file handle.\n");
	}
}

/**
 * Unlink the blocks associated with a file
 */
void delete(fname_t filename)
{
	_fs_debug1("Deleting file %d.\n", filename);

	// Wrap filename around in case it's larger than maximum supported
	if (filename > EEPROM_FS_MAX_FILES)
	{
		filename = filename % EEPROM_FS_MAX_FILES;
		_fs_debug2("Filename too large - truncated to %d.\n", filename);
	}

	// Wrap filename around in case it's larger than maximum supported
	filename = filename % EEPROM_FS_MAX_FILES;

	// Unlink data
	unlink(alloc_table[filename].data_block);
	// Delete from allocation table

	alloc_table[filename].filesize = 0;
	alloc_table[filename].data_block = NULL_PTR;

	void* alloc_offset = (void*) (EEPROM_FS_START + EEPROM_FS_ALLOC_TABLE_OFFSET
			+ filename * sizeof(file_alloc_t));
	eeprom_update_block((void*) &alloc_table[filename], alloc_offset,
			sizeof(file_alloc_t));

	_fs_debug1("File %d successfully deleted.\n", filename);
}

/**
 * Returns the EEPROM pointer to the logical block
 *
 * \param block Logical block to look up
 */
void* get_block_pointer(lba_t block)
{
	return (void*) (EEPROM_FS_START + EEPROM_FS_DATA_OFFSET
			+ ((block * EEPROM_FS_BLOCK_SIZE) % EEPROM_FS_SIZE));
}

/**
 * Returns the last logical block of a block chain
 *
 * \param block Any block in the block chain to look up
 * \return Address of last block in the block chain, or NULL for failure
 */
lba_t last_block_in_chain(lba_t block)
{
	if (block >= 0 && block < (lba_t) EEPROM_FS_NUM_BLOCKS)
	{
		_fs_debug3("Searching for last block in chain...\n");

		block_t current_block;
		current_block.next_block = block;
		do
		{
			block = current_block.next_block;
			_fs_debug4("checking... %d\n", block);
			eeprom_read_block((void*) &current_block, get_block_pointer(block),
			EEPROM_FS_BLOCK_SIZE);
		} while (current_block.next_block != NULL_PTR);

		_fs_debug3("Last block in chain: %d\n", block);

		return block;
	}
	else
	{
		_fs_error("Block %d is not part of a block chain.\n", block);
		return NULL_PTR;
	}
}

/**
 * Writes a block of data to the next free address
 * Advances the cached next_free_block
 *
 * \param data Block data to write
 * \return Address of block written, or NULL if failure
 */
lba_t write_block_data(fdata_t* data)
{
	lba_t write_to = *next_free_block;

	if (write_to >= 0 && write_to < (lba_t) EEPROM_FS_NUM_BLOCKS)
	{
		block_t current_block_data;
		eeprom_read_block((void*) &current_block_data,
				get_block_pointer(write_to),
				EEPROM_FS_BLOCK_SIZE);

		// Update next free block
		*next_free_block = current_block_data.next_block;

		_fs_debug2("Overwriting block %d...", write_to);

		// Write data only
		void* addr = get_block_pointer(write_to)
				+ (EEPROM_FS_BLOCK_SIZE - EEPROM_FS_BLOCK_DATA_SIZE);
		eeprom_write_block(data, addr, EEPROM_FS_BLOCK_DATA_SIZE);

		_fs_debug2("Done.\n");

		_fs_debug3("Next free block: %d\n", *next_free_block);

		return write_to;
	}
	else
	{
		_fs_error("Attempted to write to invalid block %d.\n", write_to);
		return NULL_PTR;
	}
}

/**
 * Link a block chain to the allocation table, marking it as a file and removing
 * it from the free block chain.
 *
 * \param fh File handle
 */
void link(file_handle_t* fh)
{
	if (fh->first_block >= 0 && fh->first_block < (lba_t) EEPROM_FS_NUM_BLOCKS)
	{
		_fs_debug1("Linking file %d to block %d.\n", fh->filename, fh->first_block);

		// Wrap filename around in case it's larger than maximum supported
		fname_t filename = fh->filename % EEPROM_FS_MAX_FILES;

		alloc_table[filename].filesize = fh->filesize;
		alloc_table[filename].data_block = fh->first_block;

		// Update table in memory
		// Data
		void* alloc_offset =
				(void*) (EEPROM_FS_START + EEPROM_FS_ALLOC_TABLE_OFFSET
						+ filename * sizeof(file_alloc_t));
		eeprom_update_block((void*) &alloc_table[filename], alloc_offset,
				sizeof(file_alloc_t));

		// New free (this needs adjustment for better write levelling)
		void* free_offset = (void*) (EEPROM_FS_START
				+ EEPROM_FS_ALLOC_TABLE_OFFSET
				+ EEPROM_FS_MAX_FILES * sizeof(file_alloc_t));
		eeprom_update_block((void*) &alloc_table[EEPROM_FS_MAX_FILES],
				free_offset, sizeof(file_alloc_t));

		_fs_debug1("Link successful.\n");
	}
	else
	{
		_fs_error("Cannot link file %d to invalid block %d.\n", fh->filename,
				fh->first_block);
	}
}

/**
 * Mark a block and all subsequent blocks in the chain as free.
 *
 * \param block Block to mark as free.
 * 				Adds block to the end of the free block chain.
 */
void unlink(lba_t block)
{
	if (block >= 0 && block < (lba_t) EEPROM_FS_NUM_BLOCKS)
	{
		_fs_debug1("Unlinking block %d.\n", block);

		// Get current last free block
		lba_t last_free = last_block_in_chain(*next_free_block);
		block_t last_free_block;
		eeprom_read_block((void*) &last_free_block,
				get_block_pointer(last_free), EEPROM_FS_BLOCK_SIZE);

		// Add new block to the end of the free block chain
		last_free_block.next_block = block;

		// Write back address only
		eeprom_write_block((void*) &last_free_block,
				get_block_pointer(last_free), sizeof(lba_t));

		_fs_debug1("Unlink successful.\n");
	}
	else
	{
		_fs_error("Cannot unlink invalid block %d.\n", block);
	}
}

/**
 * Overwrite the next_block field of a given block without touching the data
 *
 * \param block Address of block to modify
 * \param target Address of next block to link to
 */
void relink(lba_t block, lba_t target)
{
	if (block >= 0 && block < (lba_t) EEPROM_FS_NUM_BLOCKS)
	{
		if (target >= NULL_PTR && target < (lba_t) EEPROM_FS_NUM_BLOCKS)
		{
			_fs_debug3("Relinking block %d -> %d...", block, target);

			// Write address only
			eeprom_write_block((void*) &target, get_block_pointer(block),
					sizeof(lba_t));

			_fs_debug3("Done.\n");
		}
		else
		{
			_fs_error("Attempted to relink to invalid block %d.\n", target);
		}
	}
	else
	{
		_fs_error("Attempted to write to invalid block %d.\n", block);
	}
}

void dump_eeprom()
{
	uint8_t val;
	char char_buf[17];
	for (uintptr_t i = 0; i < EEPROM_FS_SIZE; i++)
	{
		// Store pure value
		val = eeprom_read_byte((uint8_t*) i);

		// Store printable ASCII character
		if ((val < 0x20) || (val > 0x7e))
			char_buf[i % 16] = '.';
		else
			char_buf[i % 16] = val;
		char_buf[16] = '\0';

		// Print line offset
		if (i % 16 == 0)
			printf("\n%#05x : ", i);

		// Print hex code
		printf("%02x ", val);

		// Print character string
		if (i % 16 == 15)
			printf(": %s", char_buf);
	}
	printf("\n");
}

/**
 * Wipe the EEPROM one dword at a time
 */
void wipe_eeprom()
{
	for (uintptr_t i = 0; i < EEPROM_FS_SIZE; i += sizeof(uint32_t))
	{
		eeprom_write_dword((uint32_t*) i, 0);
	}
}

/**
 * Set the debug level of the filesystem
 * \param level 0-4, from least to most detail
 */
void set_debug(uint8_t level)
{
	__debug = level;
}

void _fs_error(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
}

void _fs_debug1(const char *format, ...)
{
	if (__debug >= 1)
	{
		va_list args;
		va_start(args, format);
		vfprintf(stderr, format, args);
		va_end(args);
	}
}

void _fs_debug2(const char *format, ...)
{
	if (__debug >= 2)
	{
		va_list args;
		va_start(args, format);
		vfprintf(stderr, format, args);
		va_end(args);
	}
}

void _fs_debug3(const char *format, ...)
{
	if (__debug >= 3)
	{
		va_list args;
		va_start(args, format);
		vfprintf(stderr, format, args);
		va_end(args);
	}
}

void _fs_debug4(const char *format, ...)
{
	if (__debug >= 4)
	{
		va_list args;
		va_start(args, format);
		vfprintf(stderr, format, args);
		va_end(args);
	}
}
