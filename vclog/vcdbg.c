/*******************************************************************************
  Summary:
    Command line program for printing VideoCore log messages
    or assertion logs messages

  Licensing:
    Copyright (c) 2022, Joanna Rousseau - Raspberry Pi Ltd All rights reserved.
*******************************************************************************/
#include <assert.h>
#include <bits/types.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/*******************************************************************************
  Typedef & Enums & structs & Const
*******************************************************************************/

int boundary = 8; /*memory boundary in relation to counting how misaligned
                           an address is from this boundary*/

bool continuously_check_logs =
    false; /*User decides whether they want the program to continuously check for
              new logs coming in*/

enum Log_requested { /*used to parse which logs the user wants printing*/
                     log_unknown = -1,
                     log_msg = 0,
                     log_assert = 1,
};

/* STRUCTS headers below describe how VC arranges it's data within the regions
 of interest:
NOTE: Since the VC might have a different memory model from the host (32-bit vs
64-bit) structs values need to be of the same size. This code defines
fixed-width types that match the VC memory types*/

/*Individual entries header:
Asserts/msg individual messages each have a header prefixing it to describe
it's state for easy reading */
typedef struct {
  int32_t time;      // time log produced in centi format
  uint16_t seq_num;  // if two entries have same timestamp then
                     // this seq num differentiates them
  uint16_t size;     // size of entire log entry = this header + data
} Individual_msg_hdr_t;

/*Individual entries are found within a region of VC's memory, this structs
is found at the start ( the header) of this region:
In VC memory, before each 'message type' such as assert or msg,
there is a header that describes what is within this section of VC memory and
the start and end of the messages within it. This region works like a circular
buffer*/
typedef struct {
  uint8_t name[4];
  uint32_t start;  // circular buffer start for this type of message (
                   // assert/msg/etc):defined when the log is created and
                   // does not change
  uint32_t end;    // circular buffer end :defined when the log is created
                   // and does not change
  uint32_t write;  // Points to where VC will write the next msg
  uint32_t read;   /* Points to the first message (oldest message written by
      VC) for reading; the read pointer gets updated when the circular buffer is
  full and wrappes around : VC wipes the oldest message with it's latest msg*/
  Individual_msg_hdr_t header;  // Before each message there is a header that
                                // holds information about the message
} VC_region_requested_hdr_t;

/*This struct describes the whole of VC memory:
This struct is found at the beginning of VC memory and provide a table of
contents of where each message type ( assert/ task/ msg) and other info is
found*/
typedef struct {
  uint8_t padding[32];  // VC has extra information which we don't need so just
                        // adding padding
  int32_t type_assert;
  uint32_t ptr_log_assert;
  int32_t type_msg;
  uint32_t ptr_log_msg;
  int32_t type_task;
  uint32_t ptr_log_task;
} VC_Table_of_contents_t;

// this struct is used to save ( and keep together) the start and end of a memory
// region so the details can be sent to functions that check a pointer doesn't wrap
//around within the circular buffer
typedef struct {
  char *start;  // start of region in memory
  char *end;    // start of region in memory
} Mem_region_t;

// this struct keeps the same location in memory but both in physical VC memory +
//virtual mmaped region location
typedef struct {
  VC_Table_of_contents_t *actual;  // Physical location in memory of all logs
                                   // start (with maskAliasing)
  VC_Table_of_contents_t
      *virtual;  // Virtual ( mmaped) location in memory of all logs start
} phy_and_virt_toc_t;

/******************************************************************************
        Function declarations
******************************************************************************/
/*Check and parse arguments to select either log msg or assert returns -1 if
 * not type known*/
static enum Log_requested parse_cmd_args(int argc, char *argv[]);

/*Get start of VC memory where all logs are found and the size of all the logs:
 * from Device Tree*/
static bool get_logs_start_and_size_from_DT(VC_Table_of_contents_t **logStart,
                                            size_t *all_logs_size);

/*Clear top two bits - which are the VideoCore aliasing*/
static void *maskVCAliasing(uint32_t val);

/*Find offset in Actual physical memory and apply it to within the mmaped
 * region*/
static void *get_loc_in_virtual_mem(const phy_and_virt_toc_t *toc,
                                    void *curr_pos);

/*Combination of the above two functions : clear top two bits and offset the
 * pointer in the correct location within virtual memory */
static void *correct_to_be_in_virtual_loc(const phy_and_virt_toc_t *toc,
                                          uint32_t val);

/*Look inside the header/struct that prefixes all logs (Like a table of
content) to find the start of the type of logs the user requested: Function
returns the header of the message type chosen ( msg or assert)*/
static VC_region_requested_hdr_t *find_hdr_requested_log(
    const phy_and_virt_toc_t *toc, enum Log_requested type);

/*Make a copy of the txt keeping in mind it might wrap around within the
buffer - correct if this is the case*/
static bool mem_check_and_copy(char *dest, const char *src, Mem_region_t *region,
                               size_t size_to_copy);

/*Checks if the location where VC is writing it's new message (end all
  messages) is within the space in memory occupied by the current message we
  are reading, this happens when we copy during a write by VC*/
static bool vc_write_ptr_within_current_msg(
    const Individual_msg_hdr_t *hrd_msg_reading, char *VC_write_ptr,
    Mem_region_t *buffer_region);

/*Increment the pointer by offset, wrapping it when it reaches end of buffer
Function returns the location of the new pointer which can be before the
curr_position */
static char *increment_ptr_within_region(size_t offset, Mem_region_t *region,
                                         const char *curr_position);
/*Parse assert  (-a) text*/
static bool parse_and_print_assert_msg(char *txt, size_t size_of_text,
                                       int32_t time);

/* Memcpy with unaligned addresses: In certain architectures, memcpy fails if
src address is not a aligned to a boundary at the start of memcpy (and at the
end of the region copied)
dest & src MUST have the same alignment */
static void memcpy_vc_memory(char *restrict dest, const char *restrict src,
                             size_t n);

/******************************************************************************
        Main Function
******************************************************************************/

int32_t main(int32_t argc, char *argv[]) {
  // parse command line arguments to know which log we want to print
  enum Log_requested type_msg_chosen = parse_cmd_args(argc, argv);
  if (type_msg_chosen < 0) {
    fprintf(stderr,
            "Usage:\n\t%s [-f] <-m|-a>\n\t%s [--follow] <--msg|--assert>\n",
            argv[0], argv[0]);
    return EXIT_FAILURE;
  }

  /* Get Log Start and Log Size from device tree and change from bigEndian to
   * littleEndian */
  VC_Table_of_contents_t *logs_start = NULL;
  size_t all_logs_size = 0;
  if (!get_logs_start_and_size_from_DT(&logs_start, &all_logs_size)) {
    fprintf(stderr,
            "Could not read from Device Tree log starts and log size\n");
    return EXIT_FAILURE;
  }

  /* File descriptor for : MMAP using toc and all_logs_size*/
  const char *const dev_mem_path =
      "/dev/mem";  // make sure the pointer and value don't change
  int32_t dev_mem = open(dev_mem_path, O_RDONLY);
  if (dev_mem == -1) {
    fprintf(stderr, "Could not open dev/mem: %s\n", strerror(errno));
    return EXIT_FAILURE;
  }

  /* Get a virtual memory ptr so point at the same adress as VC physical
  address (where all logs begin)*/
  char *mmaped_all_logs_hdr = mmap(NULL, all_logs_size, PROT_READ, MAP_PRIVATE,
                                   dev_mem, (off_t)logs_start);

  /* file descriptor can be immediately closed without affecting mmap */
  close(dev_mem);

  if (mmaped_all_logs_hdr == MAP_FAILED) {
    fprintf(stderr, "Could not map VC memory: %s\n", strerror(errno));
    return EXIT_FAILURE;
  }

  // Save start of all logs in physical memory and the virtual mmaped adress
  // so we can later calculate offsets within physical memory
  phy_and_virt_toc_t toc = {logs_start,
                            (VC_Table_of_contents_t *)mmaped_all_logs_hdr};
  phy_and_virt_toc_t *toc_ptr = &toc;

  // FIND the pointer to the beginning of header for type of msg we want to
  // print ( msg or assert)
  const VC_region_requested_hdr_t *selected_log_header =
      find_hdr_requested_log(toc_ptr, type_msg_chosen);
  if (!selected_log_header)
    goto cleanup;
  /* from that header : Get the pointer that points to the very last message
  (which is the current VC write position) before copying the buffer directly
  from physical VC memory AND offset its location within the mmaped region*/
  char *end_all_msgs =
      correct_to_be_in_virtual_loc(toc_ptr, selected_log_header->write);
  /* AND get the start and end of the buffer*/
  const char *const buffer_start =
      correct_to_be_in_virtual_loc(toc_ptr, selected_log_header->start);
  const char *const buffer_end =
      correct_to_be_in_virtual_loc(toc_ptr, selected_log_header->end);

  /*check there was no issue finding the locations of each pointer within the
   * mmaped region*/
  if (!end_all_msgs || !buffer_start || !buffer_end)
    goto cleanup;

  /* COPY & PARSE:
  Malloc space so we can copy and parse the selected logs:*/
  /* copy the current state of 'circular buffer' that holds the logs that user
   * selected to parse:*/

  /*Buffer_start can be an unaligned address (in certain architectures this
  causes a bus error as the address is io mapping); So to copy the buffer we
  might need to make adjustements so mallocing a full boundary extra to give us
  space for this*/
  size_t buffer_size = buffer_end - buffer_start;
  void *const circular_buffer = (void *)malloc(buffer_size + boundary);
  if (!circular_buffer) {
    fprintf(stderr, "Failed to allocate buffer for VC msgs\n");
    goto cleanup;
  }

  /*Count number of bytes buffer_start is misaligned by within boundary in
  memory*/
  size_t size_unaligned = (size_t)buffer_start % boundary;
  /*align circular_buffer to be the same as buffer_start and keep track of where
  * start_circular_buffer is located*/
  char *const start_circular_buff = ((char *)circular_buffer + size_unaligned);
  /*then: Copy circular buffer so we can parse it */
  memcpy_vc_memory(start_circular_buff, buffer_start, buffer_size);

  /*Store start and end of buffer in struct ( so can pass in functions)*/
  Mem_region_t buff = {start_circular_buff, start_circular_buff + buffer_size};
  Mem_region_t *const buffer_details = &buff;

  /* Now we copied the current state, we need to parse the messages*/
  /*user might choose option to constinously search for new logs. If this is
   * false the below loop will only run once; Else we start again from where we
   * last read a message successfully*/
  int num_times_checked_if_new_logs = 0; /*counts number of times we copied 
                                          buffer and read logs*/
  bool print_logs =
      true; /*Set to true at first and changes at the end of the loop depending
               on whether the user decided to keep checking for new messages */
  const Individual_msg_hdr_t
      *header_current_msg;  /*header of first message to parse*/
  const size_t size_of_msg_header =
      sizeof(Individual_msg_hdr_t); /*Individual messages each have headers that
                                       are always the same size, which we will
                                       need to skip to parse the text*/
  char *copied_text = NULL; /*pointer to beginning of each individual message to parse*/

  /*PARSE all individual messages from the copied state of circular buffer (one message at
  a time) and if the user selected to continuously check for further messages arriving,
  keep looping indefinitely waiting for new messages to arrive*/
  while (print_logs) {
    /*PREP FOR PARSING:*/

    /*If this is the first time we parse the circular buffer:*/
    if (num_times_checked_if_new_logs == 0) {
      /* re-read VC memory to get the most recent 'start of first message' to
       * parse the buffer:*/
      header_current_msg =
          correct_to_be_in_virtual_loc(toc_ptr, selected_log_header->read);
      if (!header_current_msg) {
        fprintf(stderr,
                "Failed to offset correct position within mmaped region");
        free(circular_buffer);
        goto cleanup;
      }

      /* adjust relevant pointers to point at correct offset in circular_buffer
       * (as currently pointing to physical mem)*/
      header_current_msg =
          (Individual_msg_hdr_t *)((char *)header_current_msg - buffer_start +
                                   start_circular_buff);
      end_all_msgs = end_all_msgs - buffer_start + start_circular_buff;
    }

    /*If we already parsed circular buffer at least once and need to copy the
       new state of circular_buffer:*/
    else {
      /*sleep to give CPU time to do other tasks as this is an endless loop*/
      nanosleep((const struct timespec[]){{0, 1000000L}}, NULL);
      /*wipe all content in circular_buffer so we can re-copy it and get its new
       * current state*/
      memset(start_circular_buff, 0, buffer_size);

      /*remember header_current_msg's (from the previous iteration) position
       * that we didn't parse*/
      size_t offset_of_current_read_pos =
          (char *)header_current_msg - start_circular_buff;
      /*find current VC write position ie current end of all messages*/
      end_all_msgs =
          correct_to_be_in_virtual_loc(toc_ptr, selected_log_header->write);
      /*find it's offset position in circular_buffer*/
      size_t offset_of_current_write_pos = end_all_msgs - buffer_start;
      end_all_msgs = start_circular_buff + offset_of_current_write_pos;

      /*Calculate size between current read and end_all_msgs ie the size of
       * buffer to copy for parsing:*/
      size_t size_copying;

      /*and copy the new logs we didn't parse in previous iteration:*/
      /* if we can copy 'header_current_msg' up to 'end_all_msgs' without
       * wrapping around in circular buffer then do just one memcpy*/
      if ((char *)header_current_msg < end_all_msgs) {
        size_copying = end_all_msgs - (char *)header_current_msg;
        memcpy_vc_memory(start_circular_buff + offset_of_current_read_pos,
                         buffer_start + offset_of_current_read_pos,
                         size_copying);
      }
      /*if the new logs wrap around in the buffer*/
      else {
        /*count how much we copy until reaching end of buffer and copy that
         * first*/
        size_copying = buff.end - (char *)header_current_msg;
        size_t left_in_buffer = buff.end - (char *)header_current_msg;
        memcpy_vc_memory(start_circular_buff + offset_of_current_read_pos,
                         buffer_start + offset_of_current_read_pos,
                         left_in_buffer);
        /*then from start of buffer to the write position*/
        size_copying += (end_all_msgs - buff.start);
        memcpy_vc_memory(start_circular_buff, buffer_start,
                         offset_of_current_write_pos);
      }
    }

    /*PARSE Circular buffer:*/

    /* if the start of the message matches the end of all messages, VC has no
     * messages to display currently*/
    if ((char *)header_current_msg == end_all_msgs) {
      /*if the user didn't choose the option to keep waiting until we get a
       * message then we need to exit now:*/
      if (!continuously_check_logs) {
        fprintf(stdout, "No messages available\n");
        free(circular_buffer);
        goto cleanup;
      }
      /*else we just let the code skip to the next iteration of this while loop
      and increment the tracker of times we read circular_buffer for parsing*/
    }

    /*Parse individual log messages until we reach the current write position
     * (ie end of all messages currently in this state of circular_buffer)*/
    while ((char *)header_current_msg != end_all_msgs) {
      /* make a copy of the header details so we can access the size of the text
       * (the header may wrap around the buffer)*/
      char header[size_of_msg_header];
      if (!mem_check_and_copy(header, (char *)header_current_msg,
                              buffer_details, size_of_msg_header))
        break;
      Individual_msg_hdr_t *temp_header = (Individual_msg_hdr_t *)header;

      /* size=0 usually indicates a message that is still being written by VC so
      at present we assume it is the last message and exit OR if end_all_message
      is between the start and end of the current message: then the current
      message is corrupted (VC is currently writting to it and has not yet
      updated the end of message pointer)*/
      if (temp_header->size == 0 ||
          vc_write_ptr_within_current_msg(header_current_msg, end_all_msgs,
                                           buffer_details))
        break;

      /* get the pointer to the start of the text we want to parse:
      header or 'following text' might have wrapped around in buffer (text is
      right after header)*/
      const char *ptr_text = increment_ptr_within_region(
          size_of_msg_header, buffer_details, (char *)header_current_msg);
      if (!ptr_text)
        break;

      size_t size_of_text = temp_header->size - size_of_msg_header;
      /* if the type of message chosen is msg, part of that text is logging
      level which we don't need, so we need to remove this from the size and
      move ptr to the correct 'start of text positions'*/
      if (type_msg_chosen == log_msg) {
        size_of_text -= sizeof(uint32_t);
        ptr_text = increment_ptr_within_region(sizeof(uint32_t), buffer_details,
                                               ptr_text);
      }

      /* We need to store the full txt into a temp variable as:
        A)we need to make sure msg is null terminate in case of a corruption
        during read so can parse it
        B)can walk the string to parse it without worrying about wrapping around
        buffer
      */

      /* if this isn't the first time we parse the individual msg we need to free
      the previous copied text*/
      if (copied_text)
        free(copied_text);

      char *copied_text = (char *)calloc(size_of_text, sizeof(char));
      if (!copied_text)
        break;

      if (!mem_check_and_copy(copied_text, ptr_text, buffer_details, size_of_text))
        break;

      /* parse copied text:
      if command argv is -a or assert*/
      if (type_msg_chosen == log_assert) {
        if (!parse_and_print_assert_msg(copied_text, size_of_text,
                                        header_current_msg->time))
          // we assume here if one failed it means there was a corruption
          // and we should exit
          break;
      }

      /*if command argv is -m or msg */
      else if (type_msg_chosen == log_msg)
        /* Format of the data in VC for msg after header is:
        -32 bit logging level ( so we will use sizeof(uint32_t) to skip over
        this)
        -null terminated message*/
        fprintf(stdout, "%06i.%03i: %.*s\n", header_current_msg->time / 1000,
                header_current_msg->time % 1000, (int)size_of_text,
                copied_text);

      // find the next message
      header_current_msg = (Individual_msg_hdr_t *)((char *)header_current_msg +
                                                    header_current_msg->size);
    }

    // if we haven't freed copied_text during the while loop above then free it
    if (copied_text)
      free(copied_text);

    /*This is increased to keep track of how many times we have checked for new
     * logs (allowing the user to ask for the program to keep checking for new
     * messages)*/
    num_times_checked_if_new_logs++;
    /*if the user didn't select the follow option, 'continuously_check_logs' is
     * false and the loop ends here*/
    print_logs = continuously_check_logs;
  }

  free(circular_buffer);
cleanup:
  munmap(mmaped_all_logs_hdr, all_logs_size);
  return EXIT_SUCCESS;
}

/*******************************************************************************
        FUNCTIONS
*******************************************************************************/

enum Log_requested parse_cmd_args(int argc, char *argv[]) {
  /*Parse argv commands to select either log message or log assert */
  int assert = 0;
  int msg = 0;
  /*Optionally, the user might choose to continuously wait for new logs and parse
   * them*/
  int follow = 0;

  struct option long_options[] = {/* If either the long or short option is
                                     chosen we set the above ints to 1 */
                                  {"a", no_argument, &assert, 1},
                                  {"assert", no_argument, &assert, 1},
                                  {"m", no_argument, &msg, 1},
                                  {"msg", no_argument, &msg, 1},
                                  {"f", no_argument, &follow, 1},
                                  {"follow", no_argument, &follow, 1},
                                  {0, 0, 0, 0}};

  while (getopt_long_only(argc, argv, "", long_options, NULL) != -1)
    ;
  /*set the bool that will later determine if we continuously check for new
   * logs*/
  continuously_check_logs = follow;

  /* If the user didn't choose between 'assert' of 'msg' logs :*/
  if (!(assert ^ msg))
    return log_unknown;

  return assert ? log_assert : log_msg;
}

/*****************************************************************************/

static bool get_logs_start_and_size_from_DT(VC_Table_of_contents_t **logs_start,
                                            size_t *all_logs_size) {
  bool ret = false;
  if (!logs_start || !all_logs_size)
    goto exit;

  /*VideoCore logs start and size can be found in the device tree:*/
  const char *const filename = "/proc/device-tree/chosen/log";

  FILE *fileptr = fopen(filename, "r");
  if (!fileptr)
    goto exit;

#define NVALS 2
  uint32_t vals[NVALS];

  if (fread(vals, sizeof(uint32_t), NVALS, fileptr) != NVALS)
    goto cleanup;

  /*convert values between host and network byte order*/
  *logs_start = (VC_Table_of_contents_t *)((uintptr_t)htonl(vals[0]));
  *all_logs_size = htonl(vals[1]);
  ret = true;

cleanup:
  fclose(fileptr);
exit:
  return ret;
}

/*****************************************************************************/

static VC_region_requested_hdr_t *find_hdr_requested_log(
    const phy_and_virt_toc_t *toc, enum Log_requested type) {
  uint32_t val;
  if (!toc)
    return NULL;

  /* find the start to the mmaped region (get the table of contents)
  and find in that table ( structure that holds the start of all types of
  messages) the address of what the user chose ( msg/ assert)*/
  switch (type) {
    case log_assert:
      val = toc->virtual->ptr_log_assert;
      break;
    case log_msg:
      val = toc->virtual->ptr_log_msg;
      break;
    default:
      break;
  }

  /*This pointer is currently pointing to the physical memory of VC :
  however we need the location within the mmaped region and we need to correct
  the top two bits*/
  return correct_to_be_in_virtual_loc(toc, val);
}

/*******************************************************************************/
static void *correct_to_be_in_virtual_loc(const phy_and_virt_toc_t *toc,
                                          uint32_t val) {
  if (!toc || !val)
    return NULL;
  /*Mask top two bits of that address: Top two bits of pointer are VC aliasing
   * so we need to clear them*/
  void *result = maskVCAliasing(val);
  /*Get offset withn physical memory and add this to start of mmaped region*/
  result = get_loc_in_virtual_mem(toc, result);
  return result;
}
/*******************************************************************************/

static void *maskVCAliasing(uint32_t val) {
  return (void *)((uintptr_t)(val & 0x3fffffffU));
}

/*******************************************************************************/

static void *get_loc_in_virtual_mem(const phy_and_virt_toc_t *toc,
                                    void *curr_pos) {
  if (!toc || !curr_pos)
    return NULL;
  // Get offset in physical memory ( toc->actual - current position)
  // then add that to the beginning of the virtual memory
  return ((char *)toc->virtual + ((char *)curr_pos - (char *)toc->actual));
}

/*******************************************************************************/

bool mem_check_and_copy(char *dest, const char *src, Mem_region_t *region,
                        size_t size_to_copy) {
  if (!src || !region)
    return false;
  size_t left_in_buffer = region->end - src;

  // check if all text doesn't wrap around within the region
  if (size_to_copy <= left_in_buffer)
    memcpy(dest, src, size_to_copy);

  // else we need to copy first half message up to end region and then from
  // start up to end of text
  else {
    memcpy(dest, src, left_in_buffer);
    memcpy(dest + left_in_buffer, region->start, size_to_copy - left_in_buffer);
  }
  return true;
}

/******************************************************************************/

char *increment_ptr_within_region(size_t offset, Mem_region_t *region,
                                  const char *curr_position) {
  if (!region || !curr_position)
    return NULL;

  size_t left_in_buffer = region->end - curr_position;

  while (left_in_buffer < offset) {
    offset -= (region->end - curr_position);
    curr_position = region->start;
    left_in_buffer = region->end - curr_position;
  }
  return ((char *)curr_position + offset);
}

/******************************************************************************/

bool parse_and_print_assert_msg(char *txt, size_t size_of_text, int32_t time) {
  /* Format of the data in VC for asserts after header is:
  -null terminated filename
  -32 bit line numb
  -null terminated assertion condition
  */

  // make copy of text as we will walk the copied variable to fetch the next
  // data ( linenbr, cond_str)
  char *subsequent_data = txt;

  /* move past the first null terminated string to get to next
   * data->linenumber*/
  size_t filename_len = strnlen(txt, size_of_text);
  if (filename_len == size_of_text) {
    fprintf(stderr, "Issue with length of the log assert message\n");
    return false;
  }
  subsequent_data += (filename_len + 1);

  // save linenumber and move past linenumber
  uint32_t linenumber;
  memcpy(&linenumber, subsequent_data, sizeof(linenumber));
  subsequent_data += sizeof(linenumber);

  // the next variable to save is a null terminated string ( so even if there
  // is data after this, we can just save it as is)
  char *cond_str = subsequent_data;

  fprintf(stdout,
          "%06i.%03i: assert( %s ) failed; %s line %d\n----------------\n",
          time / 1000, time % 1000, cond_str, txt, linenumber);
  return true;
}

/*****************************************************************************/

static bool vc_write_ptr_within_current_msg(
    const Individual_msg_hdr_t *hrd_msg_reading, char *vc_write_ptr,
    Mem_region_t *buffer_region) {
  /*During a copy we might have caught a message that is currently being
  written by VC, this means VC hasn't updated it's write pointer ( which is the
  end of all our messages we want to read) and we will have a corrupted
  message*/
  if (!hrd_msg_reading || !vc_write_ptr || !buffer_region)
    return false;

  // if header or text wrapped the pointer to the end of the message we are
  // readign might be before the start of it
  char *end_msg_reading = increment_ptr_within_region(
      (size_t)hrd_msg_reading->size, buffer_region, (char *)hrd_msg_reading);
  if (!end_msg_reading)
    return false;

  // Now we know where the start and end of the message pointers are witin the
  // buffer, we need to check where the VC_write_ptr is relative to this

  // In scenario header or text has wrapped: write pointer cannot be between
  // start of region and end of messages OR start of message and end of buffer
  if (vc_write_ptr < end_msg_reading || vc_write_ptr > (char *)hrd_msg_reading)
    return false;
  // In scenario header or text has not wrapped, if the write_ptr is within
  // start and end of message
  else if (vc_write_ptr > (char *)hrd_msg_reading &&
           vc_write_ptr < end_msg_reading)
    return false;

  return true;
}

/*****************************************************************************/
static void memcpy_vc_memory(char *restrict dest, const char *restrict src,
                             size_t n) {
  // Calculate non-boundary aligned bytes at start/end of region
  size_t src_offset = (uintptr_t)src % boundary;
  size_t bytes_until_boundary = src_offset ? boundary - src_offset : 0;
  size_t bytes_over_end_boundary = (uintptr_t)(src + n) % boundary;

  // Manually copy bytes before first boundary
  size_t bytes_to_manually_copy =
      n < bytes_until_boundary ? n : bytes_until_boundary;
  n -= bytes_to_manually_copy;
  while (bytes_to_manually_copy--) {
    *dest = *src++;
    dest++;
  }

  // return if we copied all of n
  if (!n)
    return;

  // memcpy centre region starting/ending on boundaries
  int bytes_to_memcpy = n - bytes_over_end_boundary;
  if (bytes_to_memcpy) {
    memcpy(dest, src, bytes_to_memcpy);
    dest += bytes_to_memcpy;
    src += bytes_to_memcpy;
    n -= bytes_to_memcpy;
  }

  // Manually copy bytes after last boundary
  n -= bytes_over_end_boundary;
  while (bytes_over_end_boundary--) {
    *dest = *src++;
    dest++;
  }
}

/*****************************************************************************/
