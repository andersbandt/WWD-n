//*****************************************************************************
//!
//! @file wwd.c
//! @author Joe Krachey, Anders Bandt
//! @brief Impelmentation of a circular buffer
//! @version 0.9
//! @date August 25th, 2024
//!
//*****************************************************************************

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! -----------------------------------------------------------------------------------------------------------------------//
//! HEADER FILES ----------------------------------------------------------------------------------------------------------//
//! -----------------------------------------------------------------------------------------------------------------------//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Standard C99 stuff */
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>

/* Driver header files */


/* My header files */
#include "circular_buffer.h"



Circular_Buffer * circular_buffer_init(size_t capacity, size_t sz)
{
    Circular_Buffer *cb = malloc(sizeof(Circular_Buffer));
    cb->buffer = malloc(capacity * sz);
    if (cb->buffer == NULL) {
        free(cb);
        return NULL;
    }
    cb->buffer_end = (char *)cb->buffer + capacity * sz;
    cb->capacity = capacity;
    cb->count = 0;
    cb->sz = sz;
    cb->head = cb->buffer;
    cb->tail = cb->buffer;
    return cb;
}



void circular_buffer_add(Circular_Buffer *cb, const void *item)
{
    if(cb->count == cb->capacity) {
        return;
        // handle error
    }
    memcpy(cb->head, item, cb->sz);
    cb->head = (char*)cb->head + cb->sz;
    if(cb->head == cb->buffer_end) {
        cb->head = cb->buffer;
    }
    cb->count++;
}

void circular_buffer_remove(Circular_Buffer *cb, void *item)
{
    if(cb->count == 0){
        return;
        // handle error
    }
    memcpy(item, cb->tail, cb->sz);
    cb->tail = (char*)cb->tail + cb->sz;
    if(cb->tail == cb->buffer_end)
        cb->tail = cb->buffer;
    cb->count--;
}


size_t circular_buffer_get_count(Circular_Buffer *cb) {
    return cb->count;
}


int circular_buffer_get_tail(Circular_Buffer *cb) {
    return (int)cb->tail;
}


//*****************************************************************************
// Returns true if the circular buffer is empty.  Returns false if it is not.
//
// Parameters
//    buffer  :   The address of the circular buffer.
//*****************************************************************************
bool circular_buffer_empty(Circular_Buffer *buffer)
{
    if (buffer->head - buffer->tail == 0) {
        return 1;
    }
    return 0;
}


//*****************************************************************************
// Returns true if the circular buffer is full.  Returns false if it is not.
//
// Parameters
//    buffer  :   The address of the circular buffer.
//*****************************************************************************
//bool circular_buffer_full(Circular_Buffer *buffer)
//{
//    if (buffer->produce_count - buffer->consume_count == buffer->max_size) {
//        return 1;
//    }
//
//    return 0;
//}


//*****************************************************************************
// Returns a circular buffer to the heap
//
// Parameters
//    buffer  :   The address of the circular buffer.
//*****************************************************************************
void circular_buffer_delete(Circular_Buffer * buffer)
{
   free((void *)buffer->buffer);
   free(buffer);
}






//*****************************************************************************
// Initializes a circular buffer.
//
// Parameters
//    max_size:   Number of entries in the circular buffer.
//*****************************************************************************
//Circular_Buffer * circular_buffer_init(uint16_t max_size, size_t num_bytes)
//{
//   Circular_Buffer *buffer = malloc(sizeof(Circular_Buffer));
//   buffer->max_size = max_size;
//   buffer->sz = num_bytes;
//   buffer->produce_count = 0;
//   buffer->consume_count = 0;
//
//   // allocate data elements
//   buffer->data = malloc(max_size * num_bytes);
//   if (buffer->data == 0) {
//       return 0;
//   }
//
//   return buffer;
//}



//*****************************************************************************
// Adds a character to the circular buffer.
//
// Parameters
//    buffer  :   The address of the circular buffer.
//    c       :   Character to add.
//*******************************************************************************
//bool circular_buffer_add(Circular_Buffer *buffer, void *c)
//{
//    // If the circular buffer is full, return false.
//    if (circular_buffer_full(buffer)) {
//        return false;
//    }
//
////    https://stackoverflow.com/questions/827691/how-do-you-implement-a-circular-buffer-in-c
////    memcpy(buffer->data[buffer->produce_count % buffer->max_size], c, buffer->sz);
//
//    // Add the data to the circular buffer
//    //    *(buffer->data + buffer->produce_count) = c;
////    buffer->data[buffer->produce_count % buffer->max_size] = c;     // ChatGPT option with modulo operation    // ChatGPT option with modulo operation */
//
//    // Increment produce_count
//    (buffer->produce_count)++;
//
//    // Return true to indicate that the data was added
//    return true;
//}

//*****************************************************************************
// Removes the oldest character from the circular buffer.
//
// Parameters
//    buffer  :   The address of the circular buffer.
//
// Returns
//    The character removed from the circular buffer.  If the circular buffer
//    is empty, the value of the character returned is set to 0.
//*****************************************************************************
//void * circular_buffer_remove(Circular_Buffer *buffer)
//{
//    void * return_int;
//
//    // If the circular buffer is empty, return 0.
//    if (circular_buffer_empty(buffer)) {
//      return 0;
//    }
//
//    // remove the character from the circular buffer
////    return_int = *(buffer->data + buffer->consume_count);
//    // Remove the character from the circular buffer using modulo operation
//    return_int = buffer->data[buffer->consume_count % buffer->max_size];
//
//    // below line is optional, depending on requirements
//    // basically just sets the first element of the buffer to be reset 0
////    *(buffer->data) = 0;
//
//    // increment consume count
//    (buffer->consume_count)++;
//
//    // return the character
//    return return_int;
//}



/******************************************************************************
 * Circular Buffer Test 0
 *
 * Description:
 *  1. Create a circular buffer based on the size passed into the test function
 *  2. Add data to the circular buffer until it is full using a for loop.  Each time
 *     the for loop is executed, add loop count to the circular buffer.
 *  3. Verify that adding an additional byte of data fails
 *  4. Remove the data one byte at a time and verify the data removed matches
 *     the expected value
 *  5. Return the circular buffer to the heap.
 *
 ******************************************************************************/
//bool circular_buffer_test_0(int16_t size)
//{
//    Circular_Buffer *test_buffer;
//    int i ;
//    char data;
//    bool return_status;
//
//    // create a new circular buffer
//    test_buffer = circular_buffer_init(size);
//
//
//    // Using a for loop, Add the value of i to the circular buffer
//    // until it is full.
//    for(i = 0; i < size; i++)
//    {
//        // Add the next byte of data
//        bool added = circular_buffer_add(test_buffer, (char)i);
//
//        // If the data was not added, return the circular
//        // buffer to the heap and return false
//        if (!added) {
//            circular_buffer_delete(test_buffer);
//            return false;
//        }
//    }
//
//    // Verify that the buffer is full. If it is not full
//    // return the circular buffer to the heap and return false
//    if (!circular_buffer_full(test_buffer)) {
//        circular_buffer_delete(test_buffer);
//        return false;
//    }
//
//    // validate that you cannot add data to the circular buffer
//    // after it is full.  If the return value is is equal to true,
//    // return the circular buffer to the heap and return false.
//    bool added = circular_buffer_add(test_buffer, 'a');
//    if (added) {
//        circular_buffer_delete(test_buffer);
//        return false;
//    }
//
//
//    // remove the data from the circular buffer one byte at a time
//    for(i = 0; i < size; i++)
//    {
//        // Remove the next byte of data
//        char removed = circular_buffer_remove(test_buffer);
//
//        // Verify that the value returned. If the value returned
//        // is not correct, return the circular buffer to the heap
//        // and return false
//        if (removed != (char)i) {
//            circular_buffer_delete(test_buffer);
//            return false;
//        }
//
//    }
//
//    // Verify that the buffer is empty. If it is not empty
//    // return the circular buffer to the heap and return false
//    if (!circular_buffer_empty(test_buffer)) {
//        circular_buffer_delete(test_buffer);
//        return false;
//    }
//
//    // return the circular buffer to the heap.
//    circular_buffer_delete(test_buffer);
//    // success!!
//    return true;
//
//}


