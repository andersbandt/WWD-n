//*****************************************************************************
//!
//! @file bms_defs.h
//! @author Anders Bandt
//! @brief Contains definitions for interfacing with the BQ25120A BMS
//! @version 0.9
//! @date April 2024
//!
//*****************************************************************************

#ifndef SRC_IC_BMS_BMS_DEFS_H_
#define SRC_IC_BMS_BMS_DEFS_H_



// !!! TWO EXAMPLES BELOW !!! //
/*! \enum  */
//enum AMB_CancellationScheme {AMB_Disabled, AMB_estimateAndCancel, AMB_cancel};
//
///* FIFO_HIRES_EN */
//typedef enum {
//    FIFO_CONFIG5_HIRES_EN  = (0x1 << FIFO_CONFIG5_FIFO_HIRES_EN_POS),
//    FIFO_CONFIG5_HIRES_DIS = (0x0 << FIFO_CONFIG5_FIFO_HIRES_EN_POS),
//} FIFO_CONFIG5_HIRES_t;
// !!! END EXAMPLES !!! //


typedef enum {
    VOUT_1p1 = 0x01;
    VOUT_2p75 = 0x24;
    VOUT_3p3 = 0xFF;
} BMS_VOUT;



#endif /* SRC_IC_BMS_BMS_DEFS_H_ */
