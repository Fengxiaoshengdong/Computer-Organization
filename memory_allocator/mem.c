////////////////////////////////////////////////////////////////////////////////
// Main File:        mem.c
// This File:        mem.c
// Other Files:
// Semester:         CS 354 Fall 2018
//
// Author:           Yuanhang Wang
// Email:            wang2243@wisc.edu
// CS Login:         ywang
//
/////////////////////////// OTHER SOURCES OF HELP //////////////////////////////
//                   fully acknowledge and credit all sources of help,
//                   other than Instructors and TAs.
//
// Persons:          Identify persons by name, relationship to you, and email.
//                   Describe in detail the the ideas and help they provided.
//
// Online sources:   avoid web searches to solve your problems, but if you do
//                   search, be sure to include Web URLs and description of
//                   of any information you find.
//////////////////////////// 80 columns wide ///////////////////////////////////

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "mem.h"

/*
 * This structure serves as the header for each allocated and free block
 * It also serves as the footer for each free block
 * The blocks are ordered in the increasing order of addresses
 */
typedef struct blk_hdr {
    int size_status;

    /*
    * Size of the block is always a multiple of 8
    * => last two bits are always zero - can be used to store other information
    *
    * LSB -> Least Significant Bit (Last Bit)
    * SLB -> Second Last Bit
    * LSB = 0 => free block
    * LSB = 1 => allocated/busy block
    * SLB = 0 => previous block is free
    * SLB = 1 => previous block is allocated/busy
    *
    * When used as the footer the last two bits should be zero
    */

    /*
    * Examples:
    *
    * For a busy block with a payload of 20 bytes (i.e. 20 bytes data + an additional 4 bytes for header)
    * Header:
    * If the previous block is allocated, size_status should be set to 27
    * If the previous block is free, size_status should be set to 25
    *
    * For a free block of size 24 bytes (including 4 bytes for header + 4 bytes for footer)
    * Header:
    * If the previous block is allocated, size_status should be set to 26
    * If the previous block is free, size_status should be set to 24
    * Footer:
    * size_status should be 24
    *
    */
} blk_hdr;

/* Global variable - This will always point to the first block
 * i.e. the block with the lowest address */
blk_hdr *first_blk = NULL;

/*
 * Note:
 *  The end of the available memory can be determined using end_mark
 *  The size_status of end_mark has a value of 1
 *
 */

/*
 * Function for allocating 'size' bytes
 * Returns address of allocated block on success
 * Returns NULL on failure
 * Here is what this function should accomplish
 * - Check for sanity of size - Return NULL when appropriate
 * - Round up size to a multiple of 8
 * - Traverse the list of blocks and allocate the best free block which can
 * - accommodate the requested size
 * - Also, when allocating a block - split it into two blocks
 * Tips: Be careful with pointer arithmetic
 */
void *Alloc_Mem(int size) {
    if (size <= 0) return NULL;
    size += 4;
    if (size % 8) size = (size / 8 + 1) * 8;
    blk_hdr *best_fit = first_blk;
    blk_hdr *current_blk = first_blk;
    int blk_size = 0;
    while (current_blk->size_status != 1) {
        if ((current_blk->size_status & 1) == 0) {  // Current block free
            blk_size = current_blk->size_status;
            if (blk_size >= size) {
                // If the best_fit is not empty or its size is smaller than
                // the desired size or the current_blk is a better fit.
                // Since I initialized the best_blk as the first_blk, so
                // all of them could occur
                if (best_fit->size_status & 1
                    || blk_size < best_fit->size_status
                    || best_fit->size_status < size) {
                    best_fit = current_blk;
                }
            }
        }
        // Move cur_blk to next blk
        current_blk += current_blk->size_status / 4;
    }

    if (best_fit->size_status >= size && ((best_fit->size_status & 1) == 0)) {
        // The size of the free blk we found
        int big_blk_size = best_fit->size_status / 8 * 8;
        best_fit->size_status = size + 3;
        if (big_blk_size > size) {
            blk_hdr new_header;                // Split the blk, add new hdr
            new_header.size_status = big_blk_size - size + 2;
            *(best_fit + (size / 4)) = new_header;

            blk_hdr new_footer;                // Split the blk, update new ftr
            new_footer.size_status = new_header.size_status - 2;
            *(best_fit + (big_blk_size / 4) - 1) = new_footer;
            // No need to change the following blk's header
        } else if (big_blk_size == size) {     // The free blk fits the new blk
            blk_hdr *next_blk = best_fit + best_fit->size_status / 4;
            if (next_blk->size_status != 1)
                next_blk->size_status += 2;    // Update the next blk's header
        }
        return best_fit + 1;  // Payload pointer
    }
    return NULL;
}

/*
 * Function for freeing up a previously allocated block
 * Argument - ptr: Address of the block to be freed up
 * Returns 0 on success
 * Returns -1 on failure
 * Here is what this function should accomplish
 * - Return -1 if ptr is NULL
 * - Return -1 if ptr is not 8 byte aligned or if the block is already freed
 * - Mark the block as free
 * - Coalesce if one or both of the immediate neighbours are free
 */
int Free_Mem(void *ptr) {
    int prev_free = 0;   // Indicate whether the previous blk is free
    int next_free = 0;   // Indicate whether the next blk is free
    if (ptr == NULL) {
        return -1;
    }
    blk_hdr *cur_header = ptr - 4;  // Header of the current blk
    // Check if the blk is invalid or free
    if ((int) ptr % 8 != 0 || (cur_header->size_status & 1) != 1) {
        return -1;
    }

    // Hdr of next blk
    blk_hdr *next_header = cur_header + cur_header->size_status / 4;

    if ((cur_header->size_status & 2) == 0)
        prev_free = 1;           // Indicate if previous blk is free
    if ((next_header->size_status & 1) == 0)
        next_free = 1;          // Indicate if the next blk is free
    blk_hdr cur_footer;         // Footer of current block
    int size = cur_header->size_status / 8 * 8;  // The size of the cur block

    // Coalesce block and update each blks' hdr
    if (!next_free && !prev_free) {     // No need to coalesce
        cur_header->size_status = size + 2;
        next_header->size_status -= 2;  // Change the p-bit of the next blk
    }

    if (next_free && !prev_free) {      // Coalesce the next block
        cur_header->size_status = size + next_header->size_status;
    }

    if (!next_free && prev_free) {     // Coalesce the prev blk
        blk_hdr *prev_footer = cur_header - 1;  // Footer of the prev blk
        // Prev hdr: p=1, a=0
        blk_hdr *prev_header = cur_header - prev_footer->size_status / 4;

        prev_header->size_status += size;
        cur_header = prev_header;
        next_header->size_status -= 2;  // Update the next header's p bit
    }

    if (next_free && prev_free) {  // Coalesce the prev and next blks
        blk_hdr *prev_footer = cur_header - 1;  // Footer of the prev blk
        blk_hdr *prev_header = cur_header - prev_footer->size_status / 4;

        // Next hdr: p=1, a=0
        prev_header->size_status += (size + next_header->size_status - 2);
        cur_header = prev_header;
    }

    size = cur_header->size_status / 8 * 8;
    cur_footer.size_status = size;  // Create footer and put in right place
    *(cur_header + size / 4 - 1) = cur_footer;
    return 0;
}

/*
 * Function used to initialize the memory allocator
 * Not intended to be called more than once by a program
 * Argument - sizeOfRegion:
 *      Specifies the size of the chunk which needs to be allocated
 * Returns 0 on success and -1 on failure
 */
int Init_Mem(int sizeOfRegion) {
    int pagesize;
    int padsize;
    int fd;
    int alloc_size;
    void *space_ptr;
    blk_hdr *end_mark;
    static int allocated_once = 0;

    if (0 != allocated_once) {
        fprintf(stderr,
                "Error:mem.c: Init_Mem has allocated space "
                "during a previous call\n");
        return -1;
    }
    if (sizeOfRegion <= 0) {
        fprintf(stderr, "Error:mem.c: Requested block size is not positive\n");
        return -1;
    }

    // Get the pagesize
    pagesize = getpagesize();

    // Calculate padsize as the padding required to round up sizeOfRegion
    // to a multiple of pagesize
    padsize = sizeOfRegion % pagesize;
    padsize = (pagesize - padsize) % pagesize;

    alloc_size = sizeOfRegion + padsize;

    // Using mmap to allocate memory
    fd = open("/dev/zero", O_RDWR);
    if (-1 == fd) {
        fprintf(stderr, "Error:mem.c: Cannot open /dev/zero\n");
        return -1;
    }
    space_ptr = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE,
                     fd, 0);
    if (MAP_FAILED == space_ptr) {
        fprintf(stderr, "Error:mem.c: mmap cannot allocate space\n");
        allocated_once = 0;
        return -1;
    }

    allocated_once = 1;

    // for double word alignement and end mark
    alloc_size -= 8;

    // To begin with there is only one big free block
    // initialize heap so that first block meets
    // double word alignement requirement
    first_blk = (blk_hdr *) space_ptr + 1;
    end_mark = (blk_hdr *) ((void *) first_blk + alloc_size);

    // Setting up the header
    first_blk->size_status = alloc_size;

    // Marking the previous block as busy
    first_blk->size_status += 2;

    // Setting up the end mark and marking it as busy
    end_mark->size_status = 1;

    // Setting up the footer
    blk_hdr *footer = (blk_hdr *) ((char *) first_blk + alloc_size - 4);
    footer->size_status = alloc_size;

    return 0;
}

/*
 * Function to be used for debugging
 * Prints out a list of all the blocks along with the following information i
 * for each block
 * No.      : serial number of the block
 * Status   : free/busy
 * Prev     : status of previous block free/busy
 * t_Begin  : address of the first byte in the block (this is where the header starts)
 * t_End    : address of the last byte in the block
 * t_Size   : size of the block (as stored in the block header) (including the header/footer)
 */
void Dump_Mem() {
    int counter;
    char status[5];
    char p_status[5];
    char *t_begin = NULL;
    char *t_end = NULL;
    int t_size;

    blk_hdr *current = first_blk;
    counter = 1;

    int busy_size = 0;
    int free_size = 0;
    int is_busy = -1;

    fprintf(stdout, "************************************Block list***\
                ********************************\n");
    fprintf(stdout, "No.\tStatus\tPrev\tt_Begin\t\tt_End\t\tt_Size\n");
    fprintf(stdout, "-------------------------------------------------\
                --------------------------------\n");

    while (current->size_status != 1) {
        t_begin = (char *) current;
        t_size = current->size_status;

        if (t_size & 1) {
            // LSB = 1 => busy block
            strcpy(status, "Busy");
            is_busy = 1;
            t_size = t_size - 1;
        } else {
            strcpy(status, "Free");
            is_busy = 0;
        }

        if (t_size & 2) {
            strcpy(p_status, "Busy");
            t_size = t_size - 2;
        } else {
            strcpy(p_status, "Free");
        }

        if (is_busy)
            busy_size += t_size;
        else
            free_size += t_size;

        t_end = t_begin + t_size - 1;

        fprintf(stdout, "%d\t%s\t%s\t0x%08lx\t0x%08lx\t%d\n", counter, status,
                p_status, (unsigned long int) t_begin,
                (unsigned long int) t_end, t_size);

        current = (blk_hdr *) ((char *) current + t_size);
        counter = counter + 1;
    }

    fprintf(stdout, "---------------------------------------------------\
                ------------------------------\n");
    fprintf(stdout, "***************************************************\
                ******************************\n");
    fprintf(stdout, "Total busy size = %d\n", busy_size);
    fprintf(stdout, "Total free size = %d\n", free_size);
    fprintf(stdout, "Total size = %d\n", busy_size + free_size);
    fprintf(stdout, "***************************************************\
                ******************************\n");
    fflush(stdout);

    return;
}
