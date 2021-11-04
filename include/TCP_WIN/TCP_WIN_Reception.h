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

#ifndef TCP_WIN_RECEPTION_H
#define TCP_WIN_RECEPTION_H

#include "TCP_WIN_Globals.h"

/*=============================================================================
 *
 * Rx functions
 *
 *=============================================================================*/

/* if true may be passed directly to user (segment expected and window is empty)
 * But pxWindow->ackno should always be used to set "BUF->ackno" */
int32_t lTCPWindowRxCheck( TCPWindow_t * pxWindow,
                           uint32_t ulSequenceNumber,
                           uint32_t ulLength,
                           uint32_t ulSpace );

/* This function will be called as soon as a FIN is received. It will return true
 * if there are no 'open' reception segments */
BaseType_t xTCPWindowRxEmpty( const TCPWindow_t * pxWindow );

#endif /* ifndef TCP_WIN_RECEPTION_H */
