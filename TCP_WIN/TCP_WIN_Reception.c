/*=============================================================================
 *
 *                ######        #    #
 *                 #    #       #    #
 *                 #    #       #    #
 *                 #    #        ####
 *                 ######         ##
 *                 #  ##         ####
 *                 #   #        #    #
 *                 #    #       #    #
 *                ###  ##       #    #
 * Rx functions
 *
 *=============================================================================*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief A expected segment has been received, see if there is overlap with earlier segments.
 *
 * @param[in] pxWindow: The descriptor of the TCP sliding windows.
 * @param[in] ulSequenceNumber: The sequence number of the segment that was received.
 * @param[in] ulLength: The number of bytes that were received.
 *
 * @return The first segment descriptor involved, or NULL when no matching descriptor was found.
 */
        static TCPSegment_t * xTCPWindowRxConfirm( const TCPWindow_t * pxWindow,
                                                   uint32_t ulSequenceNumber,
                                                   uint32_t ulLength )
        {
            TCPSegment_t * pxBest = NULL;
            const ListItem_t * pxIterator;
            uint32_t ulNextSequenceNumber = ulSequenceNumber + ulLength;
            const ListItem_t * pxEnd = ipCAST_CONST_PTR_TO_CONST_TYPE_PTR( ListItem_t, &( pxWindow->xRxSegments.xListEnd ) );
            TCPSegment_t * pxSegment;

            /* A segment has been received with sequence number 'ulSequenceNumber',
             * where 'ulCurrentSequenceNumber == ulSequenceNumber', which means that
             * exactly this segment was expected.  xTCPWindowRxConfirm() will check if
             * there is already another segment with a sequence number between (ulSequenceNumber)
             * and (ulSequenceNumber+ulLength).  Normally none will be found, because
             * the next RX segment should have a sequence number equal to
             * '(ulSequenceNumber+ulLength)'. */

            /* Iterate through all RX segments that are stored: */
            for( pxIterator = listGET_NEXT( pxEnd );
                 pxIterator != pxEnd;
                 pxIterator = listGET_NEXT( pxIterator ) )
            {
                pxSegment = ipCAST_PTR_TO_TYPE_PTR( TCPSegment_t, listGET_LIST_ITEM_OWNER( pxIterator ) );

                /* And see if there is a segment for which:
                 * 'ulSequenceNumber' <= 'pxSegment->ulSequenceNumber' < 'ulNextSequenceNumber'
                 * If there are more matching segments, the one with the lowest sequence number
                 * shall be taken */
                if( ( xSequenceGreaterThanOrEqual( pxSegment->ulSequenceNumber, ulSequenceNumber ) != 0 ) &&
                    ( xSequenceLessThan( pxSegment->ulSequenceNumber, ulNextSequenceNumber ) != 0 ) )
                {
                    if( ( pxBest == NULL ) || ( xSequenceLessThan( pxSegment->ulSequenceNumber, pxBest->ulSequenceNumber ) != 0 ) )
                    {
                        pxBest = pxSegment;
                    }
                }
            }

            if( ( pxBest != NULL ) &&
                ( ( pxBest->ulSequenceNumber != ulSequenceNumber ) || ( pxBest->lDataLength != ( int32_t ) ulLength ) ) )
            {
                FreeRTOS_debug_printf( ( "xTCPWindowRxConfirm[%u]: search %u (+%u=%u) found %u (+%d=%u)\n",
                                         pxWindow->usPeerPortNumber,
                                         ( unsigned ) ( ulSequenceNumber - pxWindow->rx.ulFirstSequenceNumber ),
                                         ( unsigned ) ulLength,
                                         ( unsigned ) ( ulSequenceNumber + ulLength - pxWindow->rx.ulFirstSequenceNumber ),
                                         ( unsigned ) ( pxBest->ulSequenceNumber - pxWindow->rx.ulFirstSequenceNumber ),
                                         ( int ) pxBest->lDataLength,
                                         ( unsigned ) ( pxBest->ulSequenceNumber + ( ( uint32_t ) pxBest->lDataLength ) - pxWindow->rx.ulFirstSequenceNumber ) ) );
            }

            return pxBest;
        }
    #endif /* ipconfgiUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Data has been received with the correct ( expected  ) sequence number.
 *        It can be added to the RX stream buffer.
 * @param[in] pxWindow: The TCP sliding window data of the socket.
 * @param[in] ulLength: The number of bytes that can be added.
 */
        static void prvTCPWindowRx_ExpectedRX( TCPWindow_t * pxWindow,
                                               uint32_t ulLength )
        {
            uint32_t ulSequenceNumber = pxWindow->rx.ulCurrentSequenceNumber;
            uint32_t ulCurrentSequenceNumber = ulSequenceNumber + ulLength;

            if( listCURRENT_LIST_LENGTH( &( pxWindow->xRxSegments ) ) != 0U )
            {
                uint32_t ulSavedSequenceNumber = ulCurrentSequenceNumber;
                TCPSegment_t * pxFound;

                /* Clean up all sequence received between ulSequenceNumber and ulSequenceNumber + ulLength since they are duplicated.
                 * If the server is forced to retransmit packets several time in a row it might send a batch of concatenated packet for speed.
                 * So we cannot rely on the packets between ulSequenceNumber and ulSequenceNumber + ulLength to be sequential and it is better to just
                 * clean them out. */
                do
                {
                    pxFound = xTCPWindowRxConfirm( pxWindow, ulSequenceNumber, ulLength );

                    if( pxFound != NULL )
                    {
                        /* Remove it because it will be passed to user directly. */
                        vTCPWindowFree( pxFound );
                    }
                } while( pxFound != NULL );

                /*  Check for following segments that are already in the
                 * queue and increment ulCurrentSequenceNumber. */
                for( ; ; )
                {
                    pxFound = xTCPWindowRxFind( pxWindow, ulCurrentSequenceNumber );

                    if( pxFound == NULL )
                    {
                        break;
                    }

                    ulCurrentSequenceNumber += ( uint32_t ) pxFound->lDataLength;

                    /* As all packet below this one have been passed to the
                     * user it can be discarded. */
                    vTCPWindowFree( pxFound );
                }

                if( ulSavedSequenceNumber != ulCurrentSequenceNumber )
                {
                    /*  After the current data-package, there is more data
                     * to be popped. */
                    pxWindow->ulUserDataLength = ulCurrentSequenceNumber - ulSavedSequenceNumber;

                    if( xTCPWindowLoggingLevel >= 1 )
                    {
                        FreeRTOS_debug_printf( ( "lTCPWindowRxCheck[%u,%u]: retran %u (Found %u bytes at %u cnt %d)\n",
                                                 pxWindow->usPeerPortNumber,
                                                 pxWindow->usOurPortNumber,
                                                 ( unsigned ) ( ulSequenceNumber - pxWindow->rx.ulFirstSequenceNumber ),
                                                 ( unsigned ) pxWindow->ulUserDataLength,
                                                 ( unsigned ) ( ulSavedSequenceNumber - pxWindow->rx.ulFirstSequenceNumber ),
                                                 ( int ) listCURRENT_LIST_LENGTH( &pxWindow->xRxSegments ) ) );
                    }
                }
            }

            pxWindow->rx.ulCurrentSequenceNumber = ulCurrentSequenceNumber;
        }
    #endif /* ipconfgiUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Data has been received with a non-expected sequence number.
 *        This function will check if the RX data can be accepted.
 * @param[in] pxWindow: The TCP sliding window data of the socket.
 * @param[in] ulSequenceNumber: The sequence number at which the data should be placed.
 * @param[in] ulLength: The number of bytes that can be added.
 * @return Return -1 if the data must be refused, otherwise it returns the
 *         offset ( from the head ) at which the data can be placed.
 */
        static int32_t prvTCPWindowRx_UnexpectedRX( TCPWindow_t * pxWindow,
                                                    uint32_t ulSequenceNumber,
                                                    uint32_t ulLength )
        {
            int32_t lReturn = -1;
            uint32_t ulLast = ulSequenceNumber + ulLength;
            uint32_t ulCurrentSequenceNumber = pxWindow->rx.ulCurrentSequenceNumber;
            TCPSegment_t * pxFound;

            /* See if there is more data in a contiguous block to make the
             * SACK describe a longer range of data. */

            /* TODO: SACK's may also be delayed for a short period
             * This is useful because subsequent packets will be SACK'd with
             * single one message
             */
            for( ; ; )
            {
                pxFound = xTCPWindowRxFind( pxWindow, ulLast );

                if( pxFound == NULL )
                {
                    break;
                }

                ulLast += ( uint32_t ) pxFound->lDataLength;
            }

            if( xTCPWindowLoggingLevel >= 1 )
            {
                FreeRTOS_debug_printf( ( "lTCPWindowRxCheck[%d,%d]: seqnr %u exp %u (dist %d) SACK to %u\n",
                                         ( int ) pxWindow->usPeerPortNumber,
                                         ( int ) pxWindow->usOurPortNumber,
                                         ( unsigned ) ( ulSequenceNumber - pxWindow->rx.ulFirstSequenceNumber ),
                                         ( unsigned ) ( ulCurrentSequenceNumber - pxWindow->rx.ulFirstSequenceNumber ),
                                         ( int ) ( ulSequenceNumber - ulCurrentSequenceNumber ), /* want this signed */
                                         ( unsigned ) ( ulLast - pxWindow->rx.ulFirstSequenceNumber ) ) );
            }

            /* Now prepare the SACK message.
             * Code OPTION_CODE_SINGLE_SACK already in network byte order. */
            pxWindow->ulOptionsData[ 0 ] = OPTION_CODE_SINGLE_SACK;

            /* First sequence number that we received. */
            pxWindow->ulOptionsData[ 1 ] = FreeRTOS_htonl( ulSequenceNumber );

            /* Last + 1 */
            pxWindow->ulOptionsData[ 2 ] = FreeRTOS_htonl( ulLast );

            /* Which make 12 (3*4) option bytes. */
            pxWindow->ucOptionLength = ( uint8_t ) ( 3U * sizeof( pxWindow->ulOptionsData[ 0 ] ) );

            pxFound = xTCPWindowRxFind( pxWindow, ulSequenceNumber );

            if( pxFound != NULL )
            {
                /* This out-of-sequence packet has been received for a
                 * second time.  It is already stored but do send a SACK
                 * again. */
                /* A negative value will be returned to indicate than error. */
            }
            else
            {
                pxFound = xTCPWindowRxNew( pxWindow, ulSequenceNumber, ( int32_t ) ulLength );

                if( pxFound == NULL )
                {
                    /* Can not send a SACK, because the segment cannot be
                     * stored. */
                    pxWindow->ucOptionLength = 0U;

                    /* Needs to be stored but there is no segment
                     * available. A negative value will be returned. */
                }
                else
                {
                    uint32_t ulIntermediateResult;

                    if( xTCPWindowLoggingLevel != 0 )
                    {
                        FreeRTOS_debug_printf( ( "lTCPWindowRxCheck[%u,%u]: seqnr %u (cnt %u)\n",
                                                 pxWindow->usPeerPortNumber,
                                                 pxWindow->usOurPortNumber,
                                                 ( unsigned ) ( ulSequenceNumber - pxWindow->rx.ulFirstSequenceNumber ),
                                                 ( unsigned ) listCURRENT_LIST_LENGTH( &pxWindow->xRxSegments ) ) );
                        FreeRTOS_flush_logging();
                    }

                    /* Return a positive value.  The packet may be accepted
                    * and stored but an earlier packet is still missing. */
                    ulIntermediateResult = ulSequenceNumber - ulCurrentSequenceNumber;
                    lReturn = ( int32_t ) ulIntermediateResult;
                }
            }

            return lReturn;
        }
    #endif /* ipconfgiUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

    #if ( ipconfigUSE_TCP_WIN == 1 )

/**
 * @brief Check what to do with a new incoming packet: store or ignore.
 *
 * @param[in] pxWindow: The descriptor of the TCP sliding windows.
 * @param[in] ulSequenceNumber: The sequence number of the packet received.
 * @param[in] ulLength: The number of bytes received.
 * @param[in] ulSpace: The available space in the RX stream buffer.
 *
 * @return 0 or positive value indicating the offset at which the packet is to
 *         be stored, -1 if the packet is to be ignored.
 */
        int32_t lTCPWindowRxCheck( TCPWindow_t * pxWindow,
                                   uint32_t ulSequenceNumber,
                                   uint32_t ulLength,
                                   uint32_t ulSpace )
        {
            uint32_t ulCurrentSequenceNumber, ulIntermediateResult = 0;
            int32_t lReturn = -1;
            int32_t lDistance;

            /* If lTCPWindowRxCheck( ) returns == 0, the packet will be passed
             * directly to user (segment is expected).  If it returns a positive
             * number, an earlier packet is missing, but this packet may be stored.
             * If negative, the packet has already been stored, or it is out-of-order,
             * or there is not enough space.
             *
             * As a side-effect, pxWindow->ulUserDataLength will get set to non-zero,
             * if more Rx data may be passed to the user after this packet. */

            ulCurrentSequenceNumber = pxWindow->rx.ulCurrentSequenceNumber;

            /* For Selective Ack (SACK), used when out-of-sequence data come in. */
            pxWindow->ucOptionLength = 0U;

            /* Non-zero if TCP-windows contains data which must be popped. */
            pxWindow->ulUserDataLength = 0U;

            if( ulCurrentSequenceNumber == ulSequenceNumber )
            {
                /* This is the packet with the lowest sequence number we're waiting
                 * for.  It can be passed directly to the rx stream. */
                if( ulLength > ulSpace )
                {
                    FreeRTOS_debug_printf( ( "lTCPWindowRxCheck: Refuse %u bytes, due to lack of space (%u)\n", ( unsigned ) ulLength, ( unsigned ) ulSpace ) );
                }
                else
                {
                    /* Packet was expected, may be passed directly to the socket
                     * buffer or application.  Store the packet at offset 0. */
                    prvTCPWindowRx_ExpectedRX( pxWindow, ulLength );
                    lReturn = 0;
                }
            }
            else if( ulCurrentSequenceNumber == ( ulSequenceNumber + 1U ) )
            {
                /* Looks like a TCP keep-alive message.  Do not accept/store Rx data
                 * ulUserDataLength = 0. Not packet out-of-sync.  Just reply to it. */
            }
            else
            {
                /* The packet is not the one expected.  See if it falls within the Rx
                 * window so it can be stored. */

                /*  An "out-of-sequence" segment was received, must have missed one.
                 * Prepare a SACK (Selective ACK). */
                uint32_t ulLast = ulSequenceNumber + ulLength;

                ulIntermediateResult = ulLast - ulCurrentSequenceNumber;
                /* The cast from unsigned long to signed long is on purpose. */
                lDistance = ( int32_t ) ulIntermediateResult;

                if( lDistance <= 0 )
                {
                    /* An earlier has been received, must be a retransmission of a
                     * packet that has been accepted already.  No need to send out a
                     * Selective ACK (SACK). */
                }
                else if( lDistance > ( int32_t ) ulSpace )
                {
                    /* The new segment is ahead of rx.ulCurrentSequenceNumber.  The
                     * sequence number of this packet is too far ahead, ignore it. */
                    FreeRTOS_debug_printf( ( "lTCPWindowRxCheck: Refuse %d+%u bytes, due to lack of space (%u)\n",
                                             ( int ) lDistance,
                                             ( unsigned ) ulLength,
                                             ( unsigned ) ulSpace ) );
                }
                else
                {
                    lReturn = prvTCPWindowRx_UnexpectedRX( pxWindow, ulSequenceNumber, ulLength );
                }
            }

            return lReturn;
        }


    #endif /* ipconfgiUSE_TCP_WIN == 1 */
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
