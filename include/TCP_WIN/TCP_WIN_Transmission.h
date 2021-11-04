/*
 * FreeRTOS+TCP V2.3.2
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://github.com/FreeRTOS
 * https://www.FreeRTOS.org
 */

#ifndef TCP_WIN_TRANSMISSION_H
#define TCP_WIN_TRANSMISSION_H

#include "TCP_WIN_Globals.h"

/*=============================================================================
 *
 * Tx functions
 *
 *=============================================================================*/

/* Adds data to the Tx-window */
int32_t lTCPWindowTxAdd( TCPWindow_t * pxWindow,
                         uint32_t ulLength,
                         int32_t lPosition,
                         int32_t lMax );

/* Check data to be sent and calculate the time period we may sleep */
BaseType_t xTCPWindowTxHasData( TCPWindow_t const * pxWindow,
                                uint32_t ulWindowSize,
                                TickType_t * pulDelay );

/* See if anything is left to be sent
 * Function will be called when a FIN has been received. Only when the TX window is clean,
 * it will return pdTRUE */
BaseType_t xTCPWindowTxDone( const TCPWindow_t * pxWindow );

/* Fetches data to be sent.
 * 'plPosition' will point to a location with the circular data buffer: txStream */
uint32_t ulTCPWindowTxGet( TCPWindow_t * pxWindow,
                           uint32_t ulWindowSize,
                           int32_t * plPosition );

/* Receive a normal ACK */
uint32_t ulTCPWindowTxAck( TCPWindow_t * pxWindow,
                           uint32_t ulSequenceNumber );

/* Receive a SACK option */
uint32_t ulTCPWindowTxSack( TCPWindow_t * pxWindow,
                            uint32_t ulFirst,
                            uint32_t ulLast );

#endif /* ifndef TCP_WIN_TRANSMISSION_H */
