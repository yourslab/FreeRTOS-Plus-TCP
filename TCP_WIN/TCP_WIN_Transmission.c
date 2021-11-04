
/*=============================================================================
 *
 *                    #########   #    #
 *                    #   #   #   #    #
 *                        #       #    #
 *                        #        ####
 *                        #         ##
 *                        #        ####
 *                        #       #    #
 *                        #       #    #
 *                      #####     #    #
 *
 * Tx functions
 *
 *=============================================================================*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Increment the position in a circular buffer of size 'lMax'.
 *
 * @param[in] lPosition: The current index in the buffer.
 * @param[in] lMax: The total number of items in this buffer.
 * @param[in] lCount: The number of bytes that must be advanced.
 *
 * @return The new incremented position, or "( lPosition + lCount ) % lMax".
 */
        static int32_t lTCPIncrementTxPosition( int32_t lPosition,
                                                int32_t lMax,
                                                int32_t lCount )
        {
            int32_t lReturn;


            /* +TCP stores data in circular buffers.  Calculate the next position to
             * store. */
            lReturn = lPosition + lCount;

            if( lReturn >= lMax )
            {
                lReturn -= lMax;
            }

            return lReturn;
        }

    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Adding data to a segment that was already in the TX queue.  It
 *        will be filled-up to a maximum of MSS ( maximum segment size ).
 *
 * @param[in] pxWindow: The descriptor of the TCP sliding windows.
 * @param[in] pxSegment: The TX segment with the highest sequence number,
 *                       i.e. the "front segment".
 * @param[in] lBytesLeft: The number of bytes that must be added.
 *
 * @return lToWrite: the number of bytes added to the segment.
 */
        static int32_t prvTCPWindowTxAdd_FrontSegment( TCPWindow_t * pxWindow,
                                                       TCPSegment_t * pxSegment,
                                                       int32_t lBytesLeft )
        {
            int32_t lToWrite = FreeRTOS_min_int32( lBytesLeft, pxSegment->lMaxLength - pxSegment->lDataLength );

            pxSegment->lDataLength += lToWrite;

            if( pxSegment->lDataLength >= pxSegment->lMaxLength )
            {
                /* This segment is full, don't add more bytes. */
                pxWindow->pxHeadSegment = NULL;
            }

            /* ulNextTxSequenceNumber is the sequence number of the next byte to
             * be stored for transmission. */
            pxWindow->ulNextTxSequenceNumber += ( uint32_t ) lToWrite;

            /* Some detailed logging, for those who're interested. */
            if( ( xTCPWindowLoggingLevel >= 2 ) && ipconfigTCP_MAY_LOG_PORT( pxWindow->usOurPortNumber ) )
            {
                FreeRTOS_debug_printf( ( "lTCPWindowTxAdd: Add %4d bytes for seqNr %u len %4d (nxt %u) pos %d\n",
                                         ( int ) lBytesLeft,
                                         ( unsigned ) ( pxSegment->ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber ),
                                         ( int ) pxSegment->lDataLength,
                                         ( unsigned ) ( pxWindow->ulNextTxSequenceNumber - pxWindow->tx.ulFirstSequenceNumber ),
                                         ( int ) pxSegment->lStreamPos ) );
                FreeRTOS_flush_logging();
            }

            return lToWrite;
        }


    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Will add data to be transmitted to the front of the segment fifo.
 *
 * @param[in] pxWindow: The descriptor of the TCP sliding windows.
 * @param[in] ulLength: The number of bytes that will be sent.
 * @param[in] lPosition: The index in the TX stream buffer.
 * @param[in] lMax: The size of the ( circular ) TX stream buffer.
 *
 * @return The number of bytes added to the sliding window for transmission.
 *
 */
        int32_t lTCPWindowTxAdd( TCPWindow_t * pxWindow,
                                 uint32_t ulLength,
                                 int32_t lPosition,
                                 int32_t lMax )
        {
            int32_t lBytesLeft = ( int32_t ) ulLength, lToWrite;
            int32_t lDone = 0;
            int32_t lBufferIndex = lPosition;
            TCPSegment_t * pxSegment = pxWindow->pxHeadSegment;

            /* Puts a message in the Tx-window (after buffer size has been
             * verified). */
            if( ( pxSegment != NULL ) &&
                ( pxSegment->lDataLength < pxSegment->lMaxLength ) &&
                ( pxSegment->u.bits.bOutstanding == pdFALSE_UNSIGNED ) &&
                ( pxSegment->lDataLength != 0 ) )
            {
                lToWrite = prvTCPWindowTxAdd_FrontSegment( pxWindow, pxSegment, lBytesLeft );
                lBytesLeft -= lToWrite;
                /* Increased the return value. */
                lDone += lToWrite;

                /* Calculate the next position in the circular data buffer, knowing
                 * its maximum length 'lMax'. */
                lBufferIndex = lTCPIncrementTxPosition( lBufferIndex, lMax, lToWrite );
            }

            while( lBytesLeft > 0 )
            {
                /* The current transmission segment is full, create new segments as
                 * needed. */
                pxSegment = xTCPWindowTxNew( pxWindow, pxWindow->ulNextTxSequenceNumber, ( int32_t ) pxWindow->usMSS );

                if( pxSegment != NULL )
                {
                    /* Store as many as needed, but no more than the maximum
                     * (MSS). */
                    lToWrite = FreeRTOS_min_int32( lBytesLeft, pxSegment->lMaxLength );

                    pxSegment->lDataLength = lToWrite;
                    pxSegment->lStreamPos = lBufferIndex;
                    lBytesLeft -= lToWrite;
                    lBufferIndex = lTCPIncrementTxPosition( lBufferIndex, lMax, lToWrite );
                    pxWindow->ulNextTxSequenceNumber += ( uint32_t ) lToWrite;
                    lDone += lToWrite;

                    /* Link this segment in the Tx-Queue. */
                    vListInsertFifo( &( pxWindow->xTxQueue ), &( pxSegment->xQueueItem ) );

                    /* Let 'pxHeadSegment' point to this segment if there is still
                     * space. */
                    if( pxSegment->lDataLength < pxSegment->lMaxLength )
                    {
                        pxWindow->pxHeadSegment = pxSegment;
                    }
                    else
                    {
                        pxWindow->pxHeadSegment = NULL;
                    }
                }
                else
                {
                    /* A sever situation: running out of segments for transmission.
                     * No more data can be sent at the moment. */
                    if( lDone != 0 )
                    {
                        FreeRTOS_debug_printf( ( "lTCPWindowTxAdd: Sorry all buffers full (cancel %d bytes)\n", ( int ) lBytesLeft ) );
                    }

                    break;
                }
            }

            return lDone;
        }


    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Returns true if there are no more outstanding TX segments.
 *
 * @param[in] pxWindow: The descriptor of the TCP sliding windows.
 *
 * @return pdTRUE if there are no more outstanding Tx segments, else pdFALSE.
 */
        BaseType_t xTCPWindowTxDone( const TCPWindow_t * pxWindow )
        {
            return listLIST_IS_EMPTY( ( &pxWindow->xTxSegments ) );
        }

    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Find out if the peer is able to receive more data.
 *
 * @param[in] pxWindow: The descriptor of the TCP sliding windows.
 * @param[in] ulWindowSize: The number of bytes in this segment.
 *
 * @return True if the peer has space in it window to receive more data.
 */
        static BaseType_t prvTCPWindowTxHasSpace( TCPWindow_t const * pxWindow,
                                                  uint32_t ulWindowSize )
        {
            uint32_t ulTxOutstanding;
            BaseType_t xHasSpace;
            const TCPSegment_t * pxSegment;
            uint32_t ulNettSize;

            /* This function will look if there is new transmission data.  It will
             * return true if there is data to be sent. */

            pxSegment = xTCPWindowPeekHead( &( pxWindow->xTxQueue ) );

            if( pxSegment == NULL )
            {
                xHasSpace = pdFALSE;
            }
            else
            {
                /* How much data is outstanding, i.e. how much data has been sent
                 * but not yet acknowledged ? */
                if( pxWindow->tx.ulHighestSequenceNumber >= pxWindow->tx.ulCurrentSequenceNumber )
                {
                    ulTxOutstanding = pxWindow->tx.ulHighestSequenceNumber - pxWindow->tx.ulCurrentSequenceNumber;
                }
                else
                {
                    ulTxOutstanding = 0U;
                }

                /* Subtract this from the peer's space. */
                ulNettSize = ulWindowSize - FreeRTOS_min_uint32( ulWindowSize, ulTxOutstanding );

                /* See if the next segment may be sent. */
                if( ulNettSize >= ( uint32_t ) pxSegment->lDataLength )
                {
                    xHasSpace = pdTRUE;
                }
                else
                {
                    xHasSpace = pdFALSE;
                }

                /* If 'xHasSpace', it looks like the peer has at least space for 1
                 * more new segment of size MSS.  xSize.ulTxWindowLength is the self-imposed
                 * limitation of the transmission window (in case of many resends it
                 * may be decreased). */
                if( ( ulTxOutstanding != 0U ) && ( pxWindow->xSize.ulTxWindowLength < ( ulTxOutstanding + ( ( uint32_t ) pxSegment->lDataLength ) ) ) )
                {
                    xHasSpace = pdFALSE;
                }
            }

            return xHasSpace;
        }


    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Returns true if there is TX data that can be sent right now.
 *
 * @param[in] pxWindow: The descriptor of the TCP sliding windows.
 * @param[in] ulWindowSize: The current size of the sliding RX window of the peer.
 * @param[out] pulDelay: The delay before the packet may be sent.
 *
 * @return pdTRUE if there is Tx data that can be sent, else pdFALSE.
 */
        BaseType_t xTCPWindowTxHasData( TCPWindow_t const * pxWindow,
                                        uint32_t ulWindowSize,
                                        TickType_t * pulDelay )
        {
            TCPSegment_t const * pxSegment;
            BaseType_t xReturn;
            TickType_t ulAge, ulMaxAge;

            *pulDelay = 0U;

            if( listLIST_IS_EMPTY( &pxWindow->xPriorityQueue ) == pdFALSE )
            {
                /* No need to look at retransmissions or new transmission as long as
                 * there are priority segments.  *pulDelay equals zero, meaning it must
                 * be sent out immediately. */
                xReturn = pdTRUE;
            }
            else
            {
                pxSegment = xTCPWindowPeekHead( &( pxWindow->xWaitQueue ) );

                if( pxSegment != NULL )
                {
                    uint32_t ulSRTT = ( uint32_t ) pxWindow->lSRTT;

                    /* There is an outstanding segment, see if it is time to resend
                     * it. */
                    ulAge = ulTimerGetAge( &pxSegment->xTransmitTimer );

                    /* After a packet has been sent for the first time, it will wait
                     * '1 * ulSRTT' ms for an ACK. A second time it will wait '2 * ulSRTT' ms,
                     * each time doubling the time-out */
                    ulMaxAge = ( ( uint32_t ) 1U << pxSegment->u.bits.ucTransmitCount );
                    ulMaxAge *= ulSRTT;

                    if( ulMaxAge > ulAge )
                    {
                        /* A segment must be sent after this amount of msecs */
                        *pulDelay = ulMaxAge - ulAge;
                    }

                    xReturn = pdTRUE;
                }
                else
                {
                    /* No priority segment, no outstanding data, see if there is new
                     * transmission data. */
                    pxSegment = xTCPWindowPeekHead( &pxWindow->xTxQueue );

                    /* See if it fits in the peer's reception window. */
                    if( pxSegment == NULL )
                    {
                        xReturn = pdFALSE;
                    }
                    else if( prvTCPWindowTxHasSpace( pxWindow, ulWindowSize ) == pdFALSE )
                    {
                        /* Too many outstanding messages. */
                        xReturn = pdFALSE;
                    }
                    else if( ( pxWindow->u.bits.bSendFullSize != pdFALSE_UNSIGNED ) && ( pxSegment->lDataLength < pxSegment->lMaxLength ) )
                    {
                        /* 'bSendFullSize' is a special optimisation.  If true, the
                         * driver will only sent completely filled packets (of MSS
                         * bytes). */
                        xReturn = pdFALSE;
                    }
                    else
                    {
                        xReturn = pdTRUE;
                    }
                }
            }

            return xReturn;
        }


    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Three type of queues are used for transmission: priority, waiting, and
 *        the normal TX queue of unsent data.  Message in the waiting queue will
 *        be sent when their timer has expired.
 * @param[in] pxWindow: The descriptor of the TCP sliding windows.
 */
        static TCPSegment_t * pxTCPWindowTx_GetWaitQueue( TCPWindow_t * pxWindow )
        {
            TCPSegment_t * pxSegment = xTCPWindowPeekHead( &( pxWindow->xWaitQueue ) );

            if( pxSegment != NULL )
            {
                /* Do check the timing. */
                uint32_t ulMaxTime;

                ulMaxTime = ( ( uint32_t ) 1U ) << pxSegment->u.bits.ucTransmitCount;
                ulMaxTime *= ( uint32_t ) pxWindow->lSRTT;

                if( ulTimerGetAge( &pxSegment->xTransmitTimer ) > ulMaxTime )
                {
                    /* A normal (non-fast) retransmission.  Move it from the
                     * head of the waiting queue. */
                    pxSegment = xTCPWindowGetHead( &( pxWindow->xWaitQueue ) );
                    pxSegment->u.bits.ucDupAckCount = ( uint8_t ) pdFALSE_UNSIGNED;

                    /* Some detailed logging. */
                    if( ( xTCPWindowLoggingLevel != 0 ) && ( ipconfigTCP_MAY_LOG_PORT( pxWindow->usOurPortNumber ) ) )
                    {
                        FreeRTOS_debug_printf( ( "ulTCPWindowTxGet[%u,%u]: WaitQueue %d bytes for sequence number %u (0x%X)\n",
                                                 pxWindow->usPeerPortNumber,
                                                 pxWindow->usOurPortNumber,
                                                 ( int ) pxSegment->lDataLength,
                                                 ( unsigned ) ( pxSegment->ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber ),
                                                 ( unsigned ) pxSegment->ulSequenceNumber ) );
                        FreeRTOS_flush_logging();
                    }
                }
                else
                {
                    pxSegment = NULL;
                }
            }

            return pxSegment;
        }
    #endif /* ipconfigUSE_TCP_WIN == 1 */

/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief See if there is a transmission in the normal TX queue. It is the
 *        first time these data are being sent. After sending they will move
 *        the waiting queue.
 * @param[in] pxWindow: The descriptor of the TCP sliding windows.
 * @param[in] ulWindowSize: The available space that the peer has in his
 *                          reception window.
 * @return Either a segment that has to be sent, or NULL.
 */
        static TCPSegment_t * pxTCPWindowTx_GetTXQueue( TCPWindow_t * pxWindow,
                                                        uint32_t ulWindowSize )
        {
            TCPSegment_t * pxSegment = xTCPWindowPeekHead( &( pxWindow->xTxQueue ) );

            if( pxSegment == NULL )
            {
                /* No segments queued. */
            }
            else if( ( pxWindow->u.bits.bSendFullSize != pdFALSE_UNSIGNED ) && ( pxSegment->lDataLength < pxSegment->lMaxLength ) )
            {
                /* A segment has been queued but the driver waits until it
                 * has a full size of MSS. */
                pxSegment = NULL;
            }
            else if( prvTCPWindowTxHasSpace( pxWindow, ulWindowSize ) == pdFALSE )
            {
                /* Peer has no more space at this moment. */
                pxSegment = NULL;
            }
            else
            {
                /* pxSegment was just obtained with a peek function,
                 * now remove it from of the Tx queue. */
                pxSegment = xTCPWindowGetHead( &( pxWindow->xTxQueue ) );

                /* Don't let pxHeadSegment point to this segment any more,
                 * so no more data will be added. */
                if( pxWindow->pxHeadSegment == pxSegment )
                {
                    pxWindow->pxHeadSegment = NULL;
                }

                /* pxWindow->tx.highest registers the highest sequence
                 * number in our transmission window. */
                pxWindow->tx.ulHighestSequenceNumber = pxSegment->ulSequenceNumber + ( ( uint32_t ) pxSegment->lDataLength );

                /* ...and more detailed logging */
                if( ( xTCPWindowLoggingLevel >= 2 ) && ( ipconfigTCP_MAY_LOG_PORT( pxWindow->usOurPortNumber ) ) )
                {
                    FreeRTOS_debug_printf( ( "ulTCPWindowTxGet[%u,%u]: XmitQueue %d bytes for sequence number %u (ws %u)\n",
                                             pxWindow->usPeerPortNumber,
                                             pxWindow->usOurPortNumber,
                                             ( int ) pxSegment->lDataLength,
                                             ( unsigned ) ( pxSegment->ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber ),
                                             ( unsigned ) ulWindowSize ) );
                    FreeRTOS_flush_logging();
                }
            }

            return pxSegment;
        }
    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Get data that can be transmitted right now. There are three types of
 *        outstanding segments: Priority queue, Waiting queue, Normal TX queue.
 *
 * @param[in] pxWindow: The descriptor of the TCP sliding windows.
 * @param[in] ulWindowSize: The current size of the sliding RX window of the peer.
 * @param[out] plPosition: The index within the TX stream buffer of the first byte to be sent.
 *
 * @return The amount of data in bytes that can be transmitted right now.
 */
        uint32_t ulTCPWindowTxGet( TCPWindow_t * pxWindow,
                                   uint32_t ulWindowSize,
                                   int32_t * plPosition )
        {
            TCPSegment_t * pxSegment;
            uint32_t ulReturn = 0U;

            /* Fetches data to be sent-out now.
             *
             * Priority messages: segments with a resend need no check current sliding
             * window size. */
            pxSegment = xTCPWindowGetHead( &( pxWindow->xPriorityQueue ) );
            pxWindow->ulOurSequenceNumber = pxWindow->tx.ulHighestSequenceNumber;

            if( pxSegment != NULL )
            {
                /* There is a priority segment. It doesn't need any checking for
                 * space or timeouts. */
                if( xTCPWindowLoggingLevel != 0 )
                {
                    FreeRTOS_debug_printf( ( "ulTCPWindowTxGet[%u,%u]: PrioQueue %d bytes for sequence number %u (ws %u)\n",
                                             pxWindow->usPeerPortNumber,
                                             pxWindow->usOurPortNumber,
                                             ( int ) pxSegment->lDataLength,
                                             ( unsigned ) ( pxSegment->ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber ),
                                             ( unsigned ) ulWindowSize ) );
                    FreeRTOS_flush_logging();
                }
            }
            else
            {
                /* Waiting messages: outstanding messages with a running timer
                 * neither check peer's reception window size because these packets
                 * have been sent earlier. */
                pxSegment = pxTCPWindowTx_GetWaitQueue( pxWindow );

                if( pxSegment == NULL )
                {
                    /* New messages: sent-out for the first time.  Check current
                     * sliding window size of peer. */
                    pxSegment = pxTCPWindowTx_GetTXQueue( pxWindow, ulWindowSize );
                }
            }

            /* See if it has already been determined to return 0. */
            if( pxSegment != NULL )
            {
                configASSERT( listLIST_ITEM_CONTAINER( &( pxSegment->xQueueItem ) ) == NULL );

                /* Now that the segment will be transmitted, add it to the tail of
                 * the waiting queue. */
                vListInsertFifo( &pxWindow->xWaitQueue, &pxSegment->xQueueItem );

                /* And mark it as outstanding. */
                pxSegment->u.bits.bOutstanding = pdTRUE_UNSIGNED;

                /* Administer the transmit count, needed for fast
                 * retransmissions. */
                ( pxSegment->u.bits.ucTransmitCount )++;

                /* If there have been several retransmissions (4), decrease the
                 * size of the transmission window to at most 2 times MSS. */
                if( ( pxSegment->u.bits.ucTransmitCount == MAX_TRANSMIT_COUNT_USING_LARGE_WINDOW ) &&
                    ( pxWindow->xSize.ulTxWindowLength > ( 2U * ( ( uint32_t ) pxWindow->usMSS ) ) ) )
                {
                    uint16_t usMSS2 = pxWindow->usMSS * 2U;
                    FreeRTOS_debug_printf( ( "ulTCPWindowTxGet[%u - %u]: Change Tx window: %u -> %u\n",
                                             pxWindow->usPeerPortNumber,
                                             pxWindow->usOurPortNumber,
                                             ( unsigned ) pxWindow->xSize.ulTxWindowLength,
                                             usMSS2 ) );
                    pxWindow->xSize.ulTxWindowLength = usMSS2;
                }

                /* Clear the transmit timer. */
                vTCPTimerSet( &( pxSegment->xTransmitTimer ) );

                pxWindow->ulOurSequenceNumber = pxSegment->ulSequenceNumber;

                /* Inform the caller where to find the data within the queue. */
                *plPosition = pxSegment->lStreamPos;

                /* And return the length of the data segment */
                ulReturn = ( uint32_t ) pxSegment->lDataLength;
            }

            return ulReturn;
        }


    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Data has been sent, and an ACK has been received. Make an estimate
 *        of the round-trip time, and calculate the new timeout for transmissions.
 *        More explanation in a comment here below.
 *
 * @param[in] pxWindow: The descriptor of the TCP sliding windows.
 * @param[in] pxSegment: The segment that was just acknowledged.
 */
        static void prvTCPWindowTxCheckAck_CalcSRTT( TCPWindow_t * pxWindow,
                                                     TCPSegment_t * pxSegment )
        {
            int32_t mS = ( int32_t ) ulTimerGetAge( &( pxSegment->xTransmitTimer ) );

            if( pxWindow->lSRTT >= mS )
            {
                /* RTT becomes smaller: adapt slowly. */
                pxWindow->lSRTT = ( ( winSRTT_DECREMENT_NEW * mS ) + ( winSRTT_DECREMENT_CURRENT * pxWindow->lSRTT ) ) / ( winSRTT_DECREMENT_NEW + winSRTT_DECREMENT_CURRENT );
            }
            else
            {
                /* RTT becomes larger: adapt quicker */
                pxWindow->lSRTT = ( ( winSRTT_INCREMENT_NEW * mS ) + ( winSRTT_INCREMENT_CURRENT * pxWindow->lSRTT ) ) / ( winSRTT_INCREMENT_NEW + winSRTT_INCREMENT_CURRENT );
            }

            /* Cap to the minimum of 50ms. */
            if( pxWindow->lSRTT < winSRTT_CAP_mS )
            {
                pxWindow->lSRTT = winSRTT_CAP_mS;
            }
        }

    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief An acknowledgement or a selective ACK (SACK) was received. See if some outstanding data
 *        may be removed from the transmission queue(s). All TX segments for which
 *        ( ( ulSequenceNumber >= ulFirst ) && ( ulSequenceNumber < ulLast ) in a contiguous block.
 *        Note that the segments are stored in xTxSegments in a strict sequential order.
 *
 * @param[in] pxWindow: The TCP-window object of the current connection.
 * @param[in] ulFirst: The sequence number of the first byte that was acknowledged.
 * @param[in] ulLast: The sequence number of the last byte ( minus one ) that was acknowledged.
 *
 * @return number of bytes that the tail of txStream may be advanced.
 */
        static uint32_t prvTCPWindowTxCheckAck( TCPWindow_t * pxWindow,
                                                uint32_t ulFirst,
                                                uint32_t ulLast )
        {
            uint32_t ulBytesConfirmed = 0U;
            uint32_t ulSequenceNumber = ulFirst, ulDataLength;
            const ListItem_t * pxIterator;
            const ListItem_t * pxEnd = ipCAST_CONST_PTR_TO_CONST_TYPE_PTR( ListItem_t, &( pxWindow->xTxSegments.xListEnd ) );
            BaseType_t xDoUnlink;
            TCPSegment_t * pxSegment;

            /* An acknowledgement or a selective ACK (SACK) was received.  See if some outstanding data
             * may be removed from the transmission queue(s).
             * All TX segments for which
             * ( ( ulSequenceNumber >= ulFirst ) && ( ulSequenceNumber < ulLast ) in a
             * contiguous block.  Note that the segments are stored in xTxSegments in a
             * strict sequential order. */

            /* SRTT[i] = (1-a) * SRTT[i-1] + a * RTT
             *
             * 0 < a < 1; usually a = 1/8
             *
             * RTO = 2 * SRTT
             *
             * where:
             * RTT is Round Trip Time
             * SRTT is Smoothed RTT
             * RTO is Retransmit timeout
             *
             * A Smoothed RTT will increase quickly, but it is conservative when
             * becoming smaller. */

            pxIterator = listGET_NEXT( pxEnd );

            while( ( pxIterator != pxEnd ) && ( xSequenceLessThan( ulSequenceNumber, ulLast ) != 0 ) )
            {
                xDoUnlink = pdFALSE;
                pxSegment = ipCAST_PTR_TO_TYPE_PTR( TCPSegment_t, listGET_LIST_ITEM_OWNER( pxIterator ) );

                /* Move to the next item because the current item might get
                 * removed. */
                pxIterator = ( const ListItem_t * ) listGET_NEXT( pxIterator );

                /* Continue if this segment does not fall within the ACK'd range. */
                if( xSequenceGreaterThan( ulSequenceNumber, pxSegment->ulSequenceNumber ) != pdFALSE )
                {
                    continue;
                }

                /* Is it ready? */
                if( ulSequenceNumber != pxSegment->ulSequenceNumber )
                {
                    /* coverity[break_stmt] : Break statement terminating the loop */
                    break;
                }

                ulDataLength = ( uint32_t ) pxSegment->lDataLength;

                if( pxSegment->u.bits.bAcked == pdFALSE_UNSIGNED )
                {
                    if( xSequenceGreaterThan( pxSegment->ulSequenceNumber + ( uint32_t ) ulDataLength, ulLast ) != pdFALSE )
                    {
                        /* What happens?  Only part of this segment was accepted,
                         * probably due to WND limits
                         *
                         * AAAAAAA BBBBBBB << acked
                         * aaaaaaa aaaa    << sent */
                        #if ( ipconfigHAS_DEBUG_PRINTF != 0 )
                            {
                                uint32_t ulFirstSeq = pxSegment->ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber;
                                FreeRTOS_debug_printf( ( "prvTCPWindowTxCheckAck[%u.%u]: %u - %u Partial sequence number %u - %u\n",
                                                         pxWindow->usPeerPortNumber,
                                                         pxWindow->usOurPortNumber,
                                                         ( unsigned ) ( ulFirstSeq - pxWindow->tx.ulFirstSequenceNumber ),
                                                         ( unsigned ) ( ulLast - pxWindow->tx.ulFirstSequenceNumber ),
                                                         ( unsigned ) ulFirstSeq,
                                                         ( unsigned ) ( ulFirstSeq + ulDataLength ) ) );
                            }
                        #endif /* ( ipconfigHAS_DEBUG_PRINTF != 0 ) */
                        break;
                    }

                    /* This segment is fully ACK'd, set the flag. */
                    pxSegment->u.bits.bAcked = pdTRUE;

                    /* Calculate the RTT only if the segment was sent-out for the
                     * first time and if this is the last ACK'd segment in a range. */
                    if( ( pxSegment->u.bits.ucTransmitCount == 1U ) && ( ( pxSegment->ulSequenceNumber + ulDataLength ) == ulLast ) )
                    {
                        prvTCPWindowTxCheckAck_CalcSRTT( pxWindow, pxSegment );
                    }

                    /* Unlink it from the 3 queues, but do not destroy it (yet). */
                    xDoUnlink = pdTRUE;
                }

                /* pxSegment->u.bits.bAcked is now true.  Is it located at the left
                 * side of the transmission queue?  If so, it may be freed. */
                if( ulSequenceNumber == pxWindow->tx.ulCurrentSequenceNumber )
                {
                    if( ( xTCPWindowLoggingLevel >= 2 ) && ( ipconfigTCP_MAY_LOG_PORT( pxWindow->usOurPortNumber ) ) )
                    {
                        FreeRTOS_debug_printf( ( "prvTCPWindowTxCheckAck: %u - %u Ready sequence number %u\n",
                                                 ( unsigned ) ( ulFirst - pxWindow->tx.ulFirstSequenceNumber ),
                                                 ( unsigned ) ( ulLast - pxWindow->tx.ulFirstSequenceNumber ),
                                                 ( unsigned ) ( pxSegment->ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber ) ) );
                    }

                    /* Increase the left-hand value of the transmission window. */
                    pxWindow->tx.ulCurrentSequenceNumber += ulDataLength;

                    /* This function will return the number of bytes that the tail
                     * of txStream may be advanced. */
                    ulBytesConfirmed += ulDataLength;

                    /* All segments below tx.ulCurrentSequenceNumber may be freed. */
                    vTCPWindowFree( pxSegment );

                    /* No need to unlink it any more. */
                    xDoUnlink = pdFALSE;
                }

                if( ( xDoUnlink != pdFALSE ) && ( listLIST_ITEM_CONTAINER( &( pxSegment->xQueueItem ) ) != NULL ) )
                {
                    /* Remove item from its queues. */
                    ( void ) uxListRemove( &pxSegment->xQueueItem );
                }

                ulSequenceNumber += ulDataLength;
            }

            return ulBytesConfirmed;
        }

    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief See if there are segments that need a fast retransmission.
 *
 * @param[in] pxWindow: The descriptor of the TCP sliding windows.
 * @param[in] ulFirst: The sequence number of the first segment that must be checked.
 *
 * @return The number of segments that need a fast retransmission.
 */
        static uint32_t prvTCPWindowFastRetransmit( TCPWindow_t * pxWindow,
                                                    uint32_t ulFirst )
        {
            const ListItem_t * pxIterator;
            const ListItem_t * pxEnd;
            TCPSegment_t * pxSegment;
            uint32_t ulCount = 0U;

            /* A higher Tx block has been acknowledged.  Now iterate through the
             * xWaitQueue to find a possible condition for a FAST retransmission. */

            pxEnd = ipCAST_CONST_PTR_TO_CONST_TYPE_PTR( ListItem_t, &( pxWindow->xWaitQueue.xListEnd ) );

            pxIterator = listGET_NEXT( pxEnd );

            while( pxIterator != pxEnd )
            {
                /* Get the owner, which is a TCP segment. */
                pxSegment = ipCAST_PTR_TO_TYPE_PTR( TCPSegment_t, listGET_LIST_ITEM_OWNER( pxIterator ) );

                /* Hop to the next item before the current gets unlinked. */
                pxIterator = listGET_NEXT( pxIterator );

                /* Fast retransmission:
                 * When 3 packets with a higher sequence number have been acknowledged
                 * by the peer, it is very unlikely a current packet will ever arrive.
                 * It will be retransmitted far before the RTO. */
                if( pxSegment->u.bits.bAcked == pdFALSE_UNSIGNED )
                {
                    if( xSequenceLessThan( pxSegment->ulSequenceNumber, ulFirst ) != pdFALSE )
                    {
                        pxSegment->u.bits.ucDupAckCount++;

                        if( pxSegment->u.bits.ucDupAckCount == DUPLICATE_ACKS_BEFORE_FAST_RETRANSMIT )
                        {
                            pxSegment->u.bits.ucTransmitCount = ( uint8_t ) pdFALSE;

                            /* Not clearing 'ucDupAckCount' yet as more SACK's might come in
                             * which might lead to a second fast rexmit. */
                            if( ( xTCPWindowLoggingLevel >= 0 ) && ( ipconfigTCP_MAY_LOG_PORT( pxWindow->usOurPortNumber ) ) )
                            {
                                FreeRTOS_debug_printf( ( "prvTCPWindowFastRetransmit: Requeue sequence number %u < %u\n",
                                                         ( unsigned ) ( pxSegment->ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber ),
                                                         ( unsigned ) ( ulFirst - pxWindow->tx.ulFirstSequenceNumber ) ) );
                                FreeRTOS_flush_logging();
                            }

                            /* Remove it from xWaitQueue. */
                            ( void ) uxListRemove( &pxSegment->xQueueItem );

                            /* Add this segment to the priority queue so it gets
                             * retransmitted immediately. */
                            vListInsertFifo( &( pxWindow->xPriorityQueue ), &( pxSegment->xQueueItem ) );
                            ulCount++;
                        }
                    }
                }
            }

            return ulCount;
        }

    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Receive a normal ACK.
 *
 * @param[in] pxWindow: Window in which a data is receive.
 * @param[in] ulSequenceNumber: The sequence number of the ACK.
 *
 * @return The location where the packet should be added.
 */
        uint32_t ulTCPWindowTxAck( TCPWindow_t * pxWindow,
                                   uint32_t ulSequenceNumber )
        {
            uint32_t ulFirstSequence, ulReturn;

            /* Receive a normal ACK. */

            ulFirstSequence = pxWindow->tx.ulCurrentSequenceNumber;

            if( xSequenceLessThanOrEqual( ulSequenceNumber, ulFirstSequence ) != pdFALSE )
            {
                ulReturn = 0U;
            }
            else
            {
                ulReturn = prvTCPWindowTxCheckAck( pxWindow, ulFirstSequence, ulSequenceNumber );
            }

            return ulReturn;
        }

    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Receive a SACK option.
 *
 * @param[in] pxWindow: Window in which the data is received.
 * @param[in] ulFirst: Index of starting position of options.
 * @param[in] ulLast: Index of end position of the options.
 *
 * @return returns the number of bytes which have been acked starting from
 *         the head position.
 */
        uint32_t ulTCPWindowTxSack( TCPWindow_t * pxWindow,
                                    uint32_t ulFirst,
                                    uint32_t ulLast )
        {
            uint32_t ulAckCount;
            uint32_t ulCurrentSequenceNumber = pxWindow->tx.ulCurrentSequenceNumber;

            /* Receive a SACK option. */
            ulAckCount = prvTCPWindowTxCheckAck( pxWindow, ulFirst, ulLast );
            ( void ) prvTCPWindowFastRetransmit( pxWindow, ulFirst );

            if( ( xTCPWindowLoggingLevel >= 1 ) && ( xSequenceGreaterThan( ulFirst, ulCurrentSequenceNumber ) != pdFALSE ) )
            {
                FreeRTOS_debug_printf( ( "ulTCPWindowTxSack[%u,%u]: from %u to %u (ack = %u)\n",
                                         pxWindow->usPeerPortNumber,
                                         pxWindow->usOurPortNumber,
                                         ( unsigned ) ( ulFirst - pxWindow->tx.ulFirstSequenceNumber ),
                                         ( unsigned ) ( ulLast - pxWindow->tx.ulFirstSequenceNumber ),
                                         ( unsigned ) ( pxWindow->tx.ulCurrentSequenceNumber - pxWindow->tx.ulFirstSequenceNumber ) ) );
                FreeRTOS_flush_logging();
            }

            return ulAckCount;
        }


    #endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/
