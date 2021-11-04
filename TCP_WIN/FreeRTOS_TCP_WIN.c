/*
 * FreeRTOS+TCP V2.3.4
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */


/**
 * @file FreeRTOS_TCP_WIN.c
 * @brief Module which handles the TCP windowing schemes for FreeRTOS+TCP.  Many
 * functions have two versions - one for FreeRTOS+TCP (full) and one for
 * FreeRTOS+TCP (lite).
 *
 * In this module all ports and IP addresses and sequence numbers are
 * being stored in host byte-order.
 */

/* Standard includes. */
#include <stdint.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_UDP_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IP_Private.h"

#if ( ipconfigUSE_TCP == 1 )

/* Constants used for Smoothed Round Trip Time (SRTT). */
    #define winSRTT_INCREMENT_NEW        2  /**< New increment for the smoothed RTT. */
    #define winSRTT_INCREMENT_CURRENT    6  /**< Current increment for the smoothed RTT. */
    #define winSRTT_DECREMENT_NEW        1  /**< New decrement for the smoothed RTT. */
    #define winSRTT_DECREMENT_CURRENT    7  /**< Current decrement for the smoothed RTT. */
    #define winSRTT_CAP_mS               50 /**< Cap in milliseconds. */

/**
 * @brief Utility function to cast pointer of a type to pointer of type TCPSegment_t.
 *
 * @return The casted pointer.
 */
    static portINLINE ipDECL_CAST_PTR_FUNC_FOR_TYPE( TCPSegment_t )
    {
        return ( TCPSegment_t * ) pvArgument;
    }


    #if ( ipconfigUSE_TCP_WIN == 1 )

/** @brief Create a new Rx window. */
        #define xTCPWindowRxNew( pxWindow, ulSequenceNumber, lCount )    xTCPWindowNew( pxWindow, ulSequenceNumber, lCount, pdTRUE )

/** @brief Create a new Tx window. */
        #define xTCPWindowTxNew( pxWindow, ulSequenceNumber, lCount )    xTCPWindowNew( pxWindow, ulSequenceNumber, lCount, pdFALSE )

/** @brief The code to send a single Selective ACK (SACK):
 * NOP (0x01), NOP (0x01), SACK (0x05), LEN (0x0a),
 * followed by a lower and a higher sequence number,
 * where LEN is 2 + 2*4 = 10 bytes. */
        #if ( ipconfigBYTE_ORDER == pdFREERTOS_BIG_ENDIAN )
            #define OPTION_CODE_SINGLE_SACK    ( 0x0101050aU )
        #else
            #define OPTION_CODE_SINGLE_SACK    ( 0x0a050101U )
        #endif

/** @brief Normal retransmission:
 * A packet will be retransmitted after a Retransmit Time-Out (RTO).
 * Fast retransmission:
 * When 3 packets with a higher sequence number have been acknowledged
 * by the peer, it is very unlikely a current packet will ever arrive.
 * It will be retransmitted far before the RTO.
 */
        #define DUPLICATE_ACKS_BEFORE_FAST_RETRANSMIT    ( 3U )

/** @brief If there have been several retransmissions (4), decrease the
 * size of the transmission window to at most 2 times MSS.
 */
        #define MAX_TRANSMIT_COUNT_USING_LARGE_WINDOW    ( 4U )

    #endif /* configUSE_TCP_WIN */
/*-----------------------------------------------------------*/

    static void vListInsertGeneric( List_t * const pxList,
                                    ListItem_t * const pxNewListItem,
                                    MiniListItem_t * pxWhere );

/*
 * All TCP sockets share a pool of segment descriptors (TCPSegment_t)
 * Available descriptors are stored in the 'xSegmentList'
 * When a socket owns a descriptor, it will either be stored in
 * 'xTxSegments' or 'xRxSegments'
 * As soon as a package has been confirmed, the descriptor will be returned
 * to the segment pool
 */
    #if ( ipconfigUSE_TCP_WIN == 1 )
        static BaseType_t prvCreateSectors( void );
    #endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * Find a segment with a given sequence number in the list of received
 * segments: 'pxWindow->xRxSegments'.
 */
    #if ( ipconfigUSE_TCP_WIN == 1 )
        static TCPSegment_t * xTCPWindowRxFind( const TCPWindow_t * pxWindow,
                                                uint32_t ulSequenceNumber );
    #endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * Allocate a new segment
 * The socket will borrow all segments from a common pool: 'xSegmentList',
 * which is a list of 'TCPSegment_t'
 */
    #if ( ipconfigUSE_TCP_WIN == 1 )
        static TCPSegment_t * xTCPWindowNew( TCPWindow_t * pxWindow,
                                             uint32_t ulSequenceNumber,
                                             int32_t lCount,
                                             BaseType_t xIsForRx );
    #endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * Detaches and returns the head of a queue
 */
    #if ( ipconfigUSE_TCP_WIN == 1 )
        static TCPSegment_t * xTCPWindowGetHead( const List_t * pxList );
    #endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * Returns the head of a queue but it won't be detached
 */
    #if ( ipconfigUSE_TCP_WIN == 1 )
        static TCPSegment_t * xTCPWindowPeekHead( const List_t * pxList );
    #endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * Free entry pxSegment because it's not used anymore
 * The ownership will be passed back to the segment pool
 */
    #if ( ipconfigUSE_TCP_WIN == 1 )
        static void vTCPWindowFree( TCPSegment_t * pxSegment );
    #endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * A segment has been received with sequence number 'ulSequenceNumber', where
 * 'ulCurrentSequenceNumber == ulSequenceNumber', which means that exactly this
 * segment was expected.  xTCPWindowRxConfirm() will check if there is already
 * another segment with a sequence number between (ulSequenceNumber) and
 * (ulSequenceNumber+xLength).  Normally none will be found, because the next Rx
 * segment should have a sequence number equal to '(ulSequenceNumber+xLength)'.
 */
    #if ( ipconfigUSE_TCP_WIN == 1 )
        static TCPSegment_t * xTCPWindowRxConfirm( const TCPWindow_t * pxWindow,
                                                   uint32_t ulSequenceNumber,
                                                   uint32_t ulLength );
    #endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * FreeRTOS+TCP stores data in circular buffers.  Calculate the next position to
 * store.
 */
    #if ( ipconfigUSE_TCP_WIN == 1 )
        static int32_t lTCPIncrementTxPosition( int32_t lPosition,
                                                int32_t lMax,
                                                int32_t lCount );
    #endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * This function will look if there is new transmission data.  It will return
 * true if there is data to be sent.
 */
    #if ( ipconfigUSE_TCP_WIN == 1 )
        static BaseType_t prvTCPWindowTxHasSpace( TCPWindow_t const * pxWindow,
                                                  uint32_t ulWindowSize );
    #endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * An acknowledge was received.  See if some outstanding data may be removed
 * from the transmission queue(s).
 */
    #if ( ipconfigUSE_TCP_WIN == 1 )
        static uint32_t prvTCPWindowTxCheckAck( TCPWindow_t * pxWindow,
                                                uint32_t ulFirst,
                                                uint32_t ulLast );
    #endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * A higher Tx block has been acknowledged.  Now iterate through the xWaitQueue
 * to find a possible condition for a FAST retransmission.
 */
    #if ( ipconfigUSE_TCP_WIN == 1 )
        static uint32_t prvTCPWindowFastRetransmit( TCPWindow_t * pxWindow,
                                                    uint32_t ulFirst );
    #endif /* ipconfigUSE_TCP_WIN == 1 */

/*-----------------------------------------------------------*/

/**< TCP segment pool. */
    #if ( ipconfigUSE_TCP_WIN == 1 )
        static TCPSegment_t * xTCPSegments = NULL;
    #endif /* ipconfigUSE_TCP_WIN == 1 */

/**< List of free TCP segments. */
    #if ( ipconfigUSE_TCP_WIN == 1 )
        _static List_t xSegmentList;
    #endif

/** @brief Logging verbosity level. */
    BaseType_t xTCPWindowLoggingLevel = 0;

    #if ( ipconfigUSE_TCP_WIN == 1 )
        /* Some 32-bit arithmetic: comparing sequence numbers */
        static portINLINE BaseType_t xSequenceLessThanOrEqual( uint32_t a,
                                                               uint32_t b );

/**
 * @brief Check if a <= b.
 *
 * @param[in] a: The value on the left-hand side.
 * @param[in] b: The value on the right-hand side.
 *
 * @return pdTRUE when "( b - a ) < 0x80000000". Else, pdFALSE.
 */
        static portINLINE BaseType_t xSequenceLessThanOrEqual( uint32_t a,
                                                               uint32_t b )
        {
            BaseType_t xResult = pdFALSE;

            /* Test if a <= b
             * Return true if the unsigned subtraction of (b-a) doesn't generate an
             * arithmetic overflow. */
            if( ( ( b - a ) & 0x80000000U ) == 0U )
            {
                xResult = pdTRUE;
            }

            return xResult;
        }

    #endif /* ipconfigUSE_TCP_WIN */
/*-----------------------------------------------------------*/

/**
 * @brief Check if a < b.
 *
 * @param[in] a: The value on the left-hand side.
 * @param[in] b: The value on the right-hand side.
 *
 * @return pdTRUE when "( b - ( a + 1 ) ) < 0x80000000", else pdFALSE.
 */
    BaseType_t xSequenceLessThan( uint32_t a,
                                  uint32_t b )
    {
        BaseType_t xResult = pdFALSE;

        /* Test if a < b */
        if( ( ( b - ( a + 1U ) ) & 0x80000000U ) == 0U )
        {
            xResult = pdTRUE;
        }

        return xResult;
    }

/*-----------------------------------------------------------*/

/**
 * @brief Check if a > b.
 *
 * @param[in] a: The value on the left-hand side.
 * @param[in] b: The value on the right-hand side.
 *
 * @return pdTRUE when "( a - b ) < 0x80000000", else pdFALSE.
 */
    BaseType_t xSequenceGreaterThan( uint32_t a,
                                     uint32_t b )
    {
        BaseType_t xResult = pdFALSE;

        /* Test if a > b */
        if( ( ( a - ( b + 1U ) ) & 0x80000000U ) == 0U )
        {
            xResult = pdTRUE;
        }

        return xResult;
    }


/*-----------------------------------------------------------*/
    static portINLINE BaseType_t xSequenceGreaterThanOrEqual( uint32_t a,
                                                              uint32_t b );

/**
 * @brief Test if a>=b. This function is required since the sequence numbers can roll over.
 *
 * @param[in] a: The first sequence number.
 * @param[in] b: The second sequence number.
 *
 * @return pdTRUE if a>=b, else pdFALSE.
 */

    static portINLINE BaseType_t xSequenceGreaterThanOrEqual( uint32_t a,
                                                              uint32_t b )
    {
        BaseType_t xResult = pdFALSE;

        /* Test if a >= b */
        if( ( ( a - b ) & 0x80000000U ) == 0U )
        {
            xResult = pdTRUE;
        }

        return xResult;
    }
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )
        static portINLINE void vListInsertFifo( List_t * const pxList,
                                                ListItem_t * const pxNewListItem );

/**
 * @brief Insert the given item in the list in FIFO manner.
 *
 * @param[in] pxList: The list in which the item is to inserted.
 * @param[in] pxNewListItem: The item to be inserted.
 */
        static portINLINE void vListInsertFifo( List_t * const pxList,
                                                ListItem_t * const pxNewListItem )
        {
            vListInsertGeneric( pxList, pxNewListItem, &pxList->xListEnd );
        }
    #endif
/*-----------------------------------------------------------*/

    static portINLINE void vTCPTimerSet( TCPTimer_t * pxTimer );

/**
 * @brief Set the timer's "born" time.
 *
 * @param[in] pxTimer: The TCP timer.
 */
    static portINLINE void vTCPTimerSet( TCPTimer_t * pxTimer )
    {
        pxTimer->uxBorn = xTaskGetTickCount();
    }
/*-----------------------------------------------------------*/

    static portINLINE uint32_t ulTimerGetAge( const TCPTimer_t * pxTimer );

/**
 * @brief Get the timer age in milliseconds.
 *
 * @param[in] pxTimer: The timer whose age is to be fetched.
 *
 * @return The time in milliseconds since the timer was born.
 */
    static portINLINE uint32_t ulTimerGetAge( const TCPTimer_t * pxTimer )
    {
        TickType_t uxNow = xTaskGetTickCount();
        TickType_t uxDiff = uxNow - pxTimer->uxBorn;

        return uxDiff * portTICK_PERIOD_MS;
    }
/*-----------------------------------------------------------*/

/**
 * @brief Insert a new list item into a list.
 *
 * @param[in] pxList: The list in which the item is to be inserted.
 * @param[in] pxNewListItem: The item to be inserted.
 * @param[in] pxWhere: Where should the item be inserted.
 */
    static void vListInsertGeneric( List_t * const pxList,
                                    ListItem_t * const pxNewListItem,
                                    MiniListItem_t * pxWhere )
    {
        /* Insert a new list item into pxList, it does not sort the list,
         * but it puts the item just before xListEnd, so it will be the last item
         * returned by listGET_HEAD_ENTRY() */
        pxNewListItem->pxNext = ipCAST_PTR_TO_TYPE_PTR( ListItem_t, pxWhere );

        pxNewListItem->pxPrevious = pxWhere->pxPrevious;
        pxWhere->pxPrevious->pxNext = pxNewListItem;
        pxWhere->pxPrevious = pxNewListItem;

        /* Remember which list the item is in. */
        listLIST_ITEM_CONTAINER( pxNewListItem ) = ( struct xLIST * configLIST_VOLATILE ) pxList;

        ( pxList->uxNumberOfItems )++;
    }
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Creates a pool of 'ipconfigTCP_WIN_SEG_COUNT' sector buffers. Should be called once only.
 *
 * @return When the allocation was successful: pdPASS, otherwise pdFAIL.
 */
        static BaseType_t prvCreateSectors( void )
        {
            BaseType_t xIndex, xReturn;

            /* Allocate space for 'xTCPSegments' and store them in 'xSegmentList'. */

            vListInitialise( &xSegmentList );
            xTCPSegments = ipCAST_PTR_TO_TYPE_PTR( TCPSegment_t, pvPortMallocLarge( ( size_t ) ipconfigTCP_WIN_SEG_COUNT * sizeof( xTCPSegments[ 0 ] ) ) );

            if( xTCPSegments == NULL )
            {
                FreeRTOS_debug_printf( ( "prvCreateSectors: malloc %u failed\n",
                                         ( unsigned ) ( ipconfigTCP_WIN_SEG_COUNT * sizeof( xTCPSegments[ 0 ] ) ) ) );

                xReturn = pdFAIL;
            }
            else
            {
                /* Clear the allocated space. */
                ( void ) memset( xTCPSegments, 0, ( size_t ) ipconfigTCP_WIN_SEG_COUNT * sizeof( xTCPSegments[ 0 ] ) );

                for( xIndex = 0; xIndex < ipconfigTCP_WIN_SEG_COUNT; xIndex++ )
                {
                    /* Could call vListInitialiseItem here but all data has been
                    * nulled already.  Set the owner to a segment descriptor. */
                    listSET_LIST_ITEM_OWNER( &( xTCPSegments[ xIndex ].xSegmentItem ), ( void * ) &( xTCPSegments[ xIndex ] ) );
                    listSET_LIST_ITEM_OWNER( &( xTCPSegments[ xIndex ].xQueueItem ), ( void * ) &( xTCPSegments[ xIndex ] ) );

                    /* And add it to the pool of available segments */
                    vListInsertFifo( &xSegmentList, &( xTCPSegments[ xIndex ].xSegmentItem ) );
                }

                xReturn = pdPASS;
            }

            return xReturn;
        }

    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Find a segment with a given sequence number in the list of received segments.
 *
 * @param[in] pxWindow: The descriptor of the TCP sliding windows.
 * @param[in] ulSequenceNumber: the sequence number to look-up
 *
 * @return The address of the segment descriptor found, or NULL when not found.
 */
        static TCPSegment_t * xTCPWindowRxFind( const TCPWindow_t * pxWindow,
                                                uint32_t ulSequenceNumber )
        {
            const ListItem_t * pxIterator;
            const ListItem_t * pxEnd;
            TCPSegment_t * pxSegment, * pxReturn = NULL;

            /* Find a segment with a given sequence number in the list of received
             * segments. */
            pxEnd = ipCAST_CONST_PTR_TO_CONST_TYPE_PTR( ListItem_t, &( pxWindow->xRxSegments.xListEnd ) );

            for( pxIterator = listGET_NEXT( pxEnd );
                 pxIterator != pxEnd;
                 pxIterator = listGET_NEXT( pxIterator ) )
            {
                pxSegment = ipCAST_PTR_TO_TYPE_PTR( TCPSegment_t, listGET_LIST_ITEM_OWNER( pxIterator ) );

                if( pxSegment->ulSequenceNumber == ulSequenceNumber )
                {
                    pxReturn = pxSegment;
                    break;
                }
            }

            return pxReturn;
        }

    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Allocate a new segment object, either for transmission or reception.
 *
 * @param[in] pxWindow: The descriptor of the TCP sliding windows.
 * @param[in] ulSequenceNumber: The sequence number.
 * @param[in] lCount: The number of bytes stored in this segment.
 * @param[in] xIsForRx: True when this is a reception segment.
 *
 * @return Allocate and initialise a segment descriptor, or NULL when none was available.
 */
        static TCPSegment_t * xTCPWindowNew( TCPWindow_t * pxWindow,
                                             uint32_t ulSequenceNumber,
                                             int32_t lCount,
                                             BaseType_t xIsForRx )
        {
            TCPSegment_t * pxSegment;
            ListItem_t * pxItem;

            /* Allocate a new segment.  The socket will borrow all segments from a
             * common pool: 'xSegmentList', which is a list of 'TCPSegment_t' */
            if( listLIST_IS_EMPTY( &xSegmentList ) != pdFALSE )
            {
                /* If the TCP-stack runs out of segments, you might consider
                 * increasing 'ipconfigTCP_WIN_SEG_COUNT'. */
                FreeRTOS_debug_printf( ( "xTCPWindow%cxNew: Error: all segments occupied\n", ( xIsForRx != 0 ) ? 'R' : 'T' ) );
                pxSegment = NULL;
            }
            else
            {
                /* Pop the item at the head of the list.  Semaphore protection is
                * not required as only the IP task will call these functions.  */
                pxItem = ( ListItem_t * ) listGET_HEAD_ENTRY( &xSegmentList );
                pxSegment = ipCAST_PTR_TO_TYPE_PTR( TCPSegment_t, listGET_LIST_ITEM_OWNER( pxItem ) );

                configASSERT( pxItem != NULL );
                configASSERT( pxSegment != NULL );

                /* Remove the item from xSegmentList. */
                ( void ) uxListRemove( pxItem );

                /* Add it to either the connections' Rx or Tx queue. */
                if( xIsForRx != 0 )
                {
                    vListInsertFifo( &pxWindow->xRxSegments, pxItem );
                }
                else
                {
                    vListInsertFifo( &pxWindow->xTxSegments, pxItem );
                }

                /* And set the segment's timer to zero */
                vTCPTimerSet( &pxSegment->xTransmitTimer );

                pxSegment->u.ulFlags = 0;
                pxSegment->u.bits.bIsForRx = ( xIsForRx != 0 ) ? 1U : 0U;
                pxSegment->lMaxLength = lCount;
                pxSegment->lDataLength = lCount;
                pxSegment->ulSequenceNumber = ulSequenceNumber;
                #if ( ipconfigHAS_DEBUG_PRINTF != 0 )
                    {
                        static UBaseType_t xLowestLength = ipconfigTCP_WIN_SEG_COUNT;
                        UBaseType_t xLength = listCURRENT_LIST_LENGTH( &xSegmentList );

                        if( xLowestLength > xLength )
                        {
                            xLowestLength = xLength;
                        }
                    }
                #endif /* ipconfigHAS_DEBUG_PRINTF */
            }

            return pxSegment;
        }


    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief See if the peer has more packets for this node, before allowing to shut down the connection.
 *
 * @param[in] pxWindow: The descriptor of the TCP sliding windows.
 *
 * @return pdTRUE if the connection can be closed. Else, pdFALSE.
 */
        BaseType_t xTCPWindowRxEmpty( const TCPWindow_t * pxWindow )
        {
            BaseType_t xReturn;

            /* When the peer has a close request (FIN flag), the driver will check
             * if there are missing packets in the Rx-queue.  It will accept the
             * closure of the connection if both conditions are true:
             * - the Rx-queue is empty
             * - the highest Rx sequence number has been ACK'ed */
            if( listLIST_IS_EMPTY( ( &pxWindow->xRxSegments ) ) == pdFALSE )
            {
                /* Rx data has been stored while earlier packets were missing. */
                xReturn = pdFALSE;
            }
            else if( xSequenceGreaterThanOrEqual( pxWindow->rx.ulCurrentSequenceNumber, pxWindow->rx.ulHighestSequenceNumber ) != pdFALSE )
            {
                /* No Rx packets are being stored and the highest sequence number
                 * that has been received has been ACKed. */
                xReturn = pdTRUE;
            }
            else
            {
                FreeRTOS_debug_printf( ( "xTCPWindowRxEmpty: cur %u highest %u (empty)\n",
                                         ( unsigned ) ( pxWindow->rx.ulCurrentSequenceNumber - pxWindow->rx.ulFirstSequenceNumber ),
                                         ( unsigned ) ( pxWindow->rx.ulHighestSequenceNumber - pxWindow->rx.ulFirstSequenceNumber ) ) );
                xReturn = pdFALSE;
            }

            return xReturn;
        }


    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Remove the head item of a list (generic function).
 *
 * @param[in] pxList: The list of segment descriptors.
 *
 * @return The address of the segment descriptor, or NULL when not found.
 */
        static TCPSegment_t * xTCPWindowGetHead( const List_t * pxList )
        {
            TCPSegment_t * pxSegment;
            ListItem_t * pxItem;

            /* Detaches and returns the head of a queue. */
            if( listLIST_IS_EMPTY( pxList ) != pdFALSE )
            {
                pxSegment = NULL;
            }
            else
            {
                pxItem = ( ListItem_t * ) listGET_HEAD_ENTRY( pxList );
                pxSegment = ipCAST_PTR_TO_TYPE_PTR( TCPSegment_t, listGET_LIST_ITEM_OWNER( pxItem ) );

                ( void ) uxListRemove( pxItem );
            }

            return pxSegment;
        }

    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Return the head item of a list (generic function).
 *
 * @param[in] pxList: The list of segment descriptors.
 *
 * @return The address of the segment descriptor, or NULL when the list is empty.
 */
        static TCPSegment_t * xTCPWindowPeekHead( const List_t * pxList )
        {
            const ListItem_t * pxItem;
            TCPSegment_t * pxReturn;

            /* Returns the head of a queue but it won't be detached. */
            if( listLIST_IS_EMPTY( pxList ) != pdFALSE )
            {
                pxReturn = NULL;
            }
            else
            {
                pxItem = ( ListItem_t * ) listGET_HEAD_ENTRY( pxList );
                pxReturn = ipCAST_PTR_TO_TYPE_PTR( TCPSegment_t, listGET_LIST_ITEM_OWNER( pxItem ) );
            }

            return pxReturn;
        }

    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Release a segment object, return it to the list of available segment holders.
 *
 * @param[in] pxSegment: The segment descriptor that must be freed.
 */
        static void vTCPWindowFree( TCPSegment_t * pxSegment )
        {
            /*  Free entry pxSegment because it's not used any more.  The ownership
             * will be passed back to the segment pool.
             *
             * Unlink it from one of the queues, if any. */
            if( listLIST_ITEM_CONTAINER( &( pxSegment->xQueueItem ) ) != NULL )
            {
                ( void ) uxListRemove( &( pxSegment->xQueueItem ) );
            }

            pxSegment->ulSequenceNumber = 0U;
            pxSegment->lDataLength = 0;
            pxSegment->u.ulFlags = 0U;

            /* Take it out of xRxSegments/xTxSegments */
            if( listLIST_ITEM_CONTAINER( &( pxSegment->xSegmentItem ) ) != NULL )
            {
                ( void ) uxListRemove( &( pxSegment->xSegmentItem ) );
            }

            /* Return it to xSegmentList */
            vListInsertFifo( &xSegmentList, &( pxSegment->xSegmentItem ) );
        }


    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Return all segment descriptor to the poll of descriptors, before deleting a socket.
 *
 * @param[in] pxWindow: The descriptor of the TCP sliding windows.
 */
        void vTCPWindowDestroy( TCPWindow_t const * pxWindow )
        {
            const List_t * pxSegments;
            BaseType_t xRound;
            TCPSegment_t * pxSegment;

            /*  Destroy a window.  A TCP window doesn't serve any more.  Return all
             * owned segments to the pool.  In order to save code, it will make 2 rounds,
             * one to remove the segments from xRxSegments, and a second round to clear
             * xTxSegments*/
            for( xRound = 0; xRound < 2; xRound++ )
            {
                if( xRound != 0 )
                {
                    pxSegments = &( pxWindow->xRxSegments );
                }
                else
                {
                    pxSegments = &( pxWindow->xTxSegments );
                }

                if( listLIST_IS_INITIALISED( pxSegments ) )
                {
                    while( listCURRENT_LIST_LENGTH( pxSegments ) > 0U )
                    {
                        pxSegment = ipCAST_PTR_TO_TYPE_PTR( TCPSegment_t, listGET_OWNER_OF_HEAD_ENTRY( pxSegments ) );
                        vTCPWindowFree( pxSegment );
                    }
                }
            }
        }


    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

/**
 * @brief Create a window for TCP.
 *
 * @param[in] pxWindow: The window to be created.
 * @param[in] ulRxWindowLength: The length of the receive window.
 * @param[in] ulTxWindowLength: The length of the transmit window.
 * @param[in] ulAckNumber: The first ACK number.
 * @param[in] ulSequenceNumber: The first sequence number.
 * @param[in] ulMSS: The MSS of the connection.
 */
    void vTCPWindowCreate( TCPWindow_t * pxWindow,
                           uint32_t ulRxWindowLength,
                           uint32_t ulTxWindowLength,
                           uint32_t ulAckNumber,
                           uint32_t ulSequenceNumber,
                           uint32_t ulMSS )
    {
        /* Create and initialize a window. */

        #if ( ipconfigUSE_TCP_WIN == 1 )
            {
                if( xTCPSegments == NULL )
                {
                    ( void ) prvCreateSectors();
                }

                vListInitialise( &( pxWindow->xTxSegments ) );
                vListInitialise( &( pxWindow->xRxSegments ) );

                vListInitialise( &( pxWindow->xPriorityQueue ) ); /* Priority queue: segments which must be sent immediately */
                vListInitialise( &( pxWindow->xTxQueue ) );       /* Transmit queue: segments queued for transmission */
                vListInitialise( &( pxWindow->xWaitQueue ) );     /* Waiting queue:  outstanding segments */
            }
        #endif /* ipconfigUSE_TCP_WIN == 1 */

        if( xTCPWindowLoggingLevel != 0 )
        {
            FreeRTOS_debug_printf( ( "vTCPWindowCreate: for WinLen = Rx/Tx: %u/%u\n",
                                     ( unsigned ) ulRxWindowLength, ( unsigned ) ulTxWindowLength ) );
        }

        pxWindow->xSize.ulRxWindowLength = ulRxWindowLength;
        pxWindow->xSize.ulTxWindowLength = ulTxWindowLength;

        vTCPWindowInit( pxWindow, ulAckNumber, ulSequenceNumber, ulMSS );
    }
/*-----------------------------------------------------------*/

/**
 * @brief Initialise a TCP window.
 *
 * @param[in] pxWindow: The window to be initialised.
 * @param[in] ulAckNumber: The number of the first ACK.
 * @param[in] ulSequenceNumber: The first sequence number.
 * @param[in] ulMSS: The MSS of the connection.
 */
    void vTCPWindowInit( TCPWindow_t * pxWindow,
                         uint32_t ulAckNumber,
                         uint32_t ulSequenceNumber,
                         uint32_t ulMSS )
    {
        const int32_t l500ms = 500;

        pxWindow->u.ulFlags = 0U;
        pxWindow->u.bits.bHasInit = pdTRUE_UNSIGNED;

        if( ulMSS != 0U )
        {
            if( pxWindow->usMSSInit != 0U )
            {
                pxWindow->usMSSInit = ( uint16_t ) ulMSS;
            }

            if( ( ulMSS < ( uint32_t ) pxWindow->usMSS ) || ( pxWindow->usMSS == 0U ) )
            {
                pxWindow->xSize.ulRxWindowLength = ( pxWindow->xSize.ulRxWindowLength / ulMSS ) * ulMSS;
                pxWindow->usMSS = ( uint16_t ) ulMSS;
            }
        }

        #if ( ipconfigUSE_TCP_WIN == 0 )
            {
                pxWindow->xTxSegment.lMaxLength = ( int32_t ) pxWindow->usMSS;
            }
        #endif /* ipconfigUSE_TCP_WIN == 1 */

        /*Start with a timeout of 2 * 500 ms (1 sec). */
        pxWindow->lSRTT = l500ms;

        /* Just for logging, to print relative sequence numbers. */
        pxWindow->rx.ulFirstSequenceNumber = ulAckNumber;

        /* The segment asked for in the next transmission. */
        pxWindow->rx.ulCurrentSequenceNumber = ulAckNumber;

        /* The right-hand side of the receive window. */
        pxWindow->rx.ulHighestSequenceNumber = ulAckNumber;

        pxWindow->tx.ulFirstSequenceNumber = ulSequenceNumber;

        /* The segment asked for in next transmission. */
        pxWindow->tx.ulCurrentSequenceNumber = ulSequenceNumber;

        /* The sequence number given to the next outgoing byte to be added is
         * maintained by lTCPWindowTxAdd(). */
        pxWindow->ulNextTxSequenceNumber = ulSequenceNumber;

        /* The right-hand side of the transmit window. */
        pxWindow->tx.ulHighestSequenceNumber = ulSequenceNumber;
        pxWindow->ulOurSequenceNumber = ulSequenceNumber;
    }
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Free the space occupied by the pool of segment descriptors, normally never used
 */
        void vTCPSegmentCleanup( void )
        {
            /* Free and clear the TCP segments pointer. This function should only be called
             * once FreeRTOS+TCP will no longer be used. No thread-safety is provided for this
             * function. */
            if( xTCPSegments != NULL )
            {
                vPortFreeLarge( xTCPSegments );
                xTCPSegments = NULL;
            }
        }


    #endif /* ipconfgiUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/


#endif /* ipconfigUSE_TCP == 1 */
