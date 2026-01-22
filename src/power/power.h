//*****************************************************************************
//!
//! @file power.h
//! @author Anders Bandt
//! @brief Power management for WWD
//! @version 0.9
//! @date January 2025
//!
//*****************************************************************************



enum power_mode {
    POWER_MODE_ACTIVE,
    POWER_MODE_IDLE,
    POWER_MODE_SLEEP,
    POWER_MODE_DEEP_SLEEP
};