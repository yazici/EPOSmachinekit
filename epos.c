/*

epos.c

Main routines for the EPOS drives
*/

#include "epos.h"

#include <dcf.h>

#define EPOS_PDO_MAX     4

epos_error_t epos_error_table[] = {
    {0x0000, "No error", "No error is present"},
    {0x1000, "Generic error", "Unspecific error occurred"},
    {0x2310, "Over Current error", "Short circuit in the motor winding\n\
Power supply can not supply enough acceleration current\n\
Too high Controller Gains (Velocity control parameter set, Position control parameter set)\n\
Profile acceleration and/or Profile deceleration too high\n\
Damaged power stage"},
    {0x3210, "Over Voltage error", "The power supply voltage is too high"},
    {0x3220, "Under Voltage error", "The supply voltage is too low for operation.\n\
The power supply can’t supply the acceleration current"},
    {0x4210, "Over Temperature error", "The temperature at the device power stage is too high (only on EPOS 24/5,\n\
EPOS 70/10 and MCD EPOS 60 W)"},
    {0x5113, "Supply voltage (+5V) too low", "There is a overload on internal generated 5V supply by the hall sensor connector or\n\
encoder connector (only on EPOS 24/5)"},
    {0x6100, "Internal software error", "Internal software error occurred"},
    {0x6320, "Software Parameter error", "Too high Target position with too low Profile velocity"},
    {0x7320, "Sensor Position error", "The detected position from position sensor is no longer valid in case of:\n\
- Changed Position Sensor Parameters\n\
- Wrong Position Sensor Parameters\n\
- Other Errors which influences the absolute position detection (Hall Sensor Error, Encoder Index Error, ...)"},
    {0x8110, "CAN Overrun Error (Objects lost)",""},
    {0x8111, "CAN Overrun Error", ""},
    {0x8120, "CAN Passive Mode Error", ""},
    {0x8130, "CAN Life Guard Error", ""},
    {0x8150, "CAN Transmit COB-ID collision", ""},
    {0x81FD, "CAN Bus Off", ""},
    {0x81FE, "CAN Rx Queue Overrun", ""},
    {0x81FF, "CAN Tx Queue Overrun", ""},
    {0x8210, "CAN PDO length Error", ""},
    {0x8611, "Following Error", ""},
    {0xFF01, "Hall Sensor Error", ""},
    {0xFF02, "Index Processing Error", ""},
    {0xFF03, "Encoder Resolution Error", ""},
    {0xFF04, "Hallsensor not found Error", ""},
    {0xFF06, "Negative Limit Error", ""},
    {0xFF07, "Positive Limit Error", ""},
    {0xFF08, "Hall Angle detection Error", ""},
    {0xFF09, "Software Position Limit Error", ""},
    {0xFF0A, "Position Sensor Breach", ""},
    {0xFF0B, "System Overloaded", ""}
};

const char * epos_error_text (UNS16 errCode) {
    int i;
    
    for (i = 0; i < sizeof(epos_error_table) / sizeof(epos_error_t); i++)
        if (epos_error_table[i].error_code == errCode)
            return epos_error_table[i].message;
        
    return "(unknown)";
}

// master EPOS drive structure
EPOS_drive_t        EPOS_drive;

/*
 * Name         : epos_add_slave
 *
 * Synopsis     : int    epos_add_slave (UNS8 slaveid)
 *
 * Arguments    : UNS8  slaveid : slave id to add
 *
 * Description  : Adds a new slave to the drive object
 * 
 * Returns      : int    0 if error, 1 if success
 */
int    epos_add_slave (UNS8 slaveid) {
    
   if (slaveid == getNodeId(EPOS_drive.d) || slaveid < 1)
        return 0;
    
    if (EPOS_drive.epos_slave_count >= MAX_EPOS_DRIVES)
        return 0;
    
    // add the node to the list
    EPOS_drive.epos_slaves[EPOS_drive.epos_slave_count] = slaveid;
    
    // setup the SDOs
    if (!setup_sdo (slaveid, EPOS_drive.epos_slave_count))
        return 0;
    
    // setup the PDO block for the node
    if (!setup_rx_pdo (slaveid, EPOS_drive.epos_slave_count))
        return 0;
    if (!setup_tx_pdo (slaveid, EPOS_drive.epos_slave_count))
        return 0;
    
    // add the DCF data to the node
    UNS32   errorCode;
    Object1F22 = (*EPOS_drive.d->scanIndexOD)(EPOS_drive.d, 0x1F22, &errorCode);
    if (errorCode != OD_SUCCESSFUL)
        return 0;
    
    if (slaveid >= Object1F22->bSubCount)
        return 0;

    dcfstream_t *nodedcf;
    if (!get_dcf_node (&EPOS_drive.dcf_data, slaveid, &nodedcf))
        return 0;
    
    Object1F22->pSubindex[slaveid].pObject = nodedcf->dcf;
    Object1F22->pSubindex[slaveid].size = nodedcf->size;
    
    // add the slave to the Network List (1F81)???
    // add the slave to the heartbeat???
    // setup the DCF PDO mappings???
    
    // node was setup
    EPOS_drive.epos_slave_count++;
    
    return 1;
}

/*
 * Name         : epos_setup_sdo
 *
 * Synopsis     : int     epos_setup_sdo (UNS8 slaveid, int idx)
 *
 * Arguments    : UNS8  slaveid : Slave ID
 *                int  idx : index of the slave in the slave table (for determining the location)
 *
 * Description  : sets up the SDOs for the slave
 * 
 * Returns      : int     
 */

int     epos_setup_sdo (UNS8 slaveid, int idx) {

    UNS32   result;
    UNS32   size;
    UNS32   COB_ID;
    // setup the client SDO for the node
    
    // transmit SDO
    COB_ID = 0x600 + slaveid;
    result = writeLocalDict (EPOS_drive.d,
        0x1280 + idx, 0x01, &COB_ID, sizeof(COB_ID), 0);
    if (result != OD_SUCCESSFUL)
        return 0;

    // receive SDO
    COB_ID = 0x580 + slaveid;
    result = writeLocalDict (EPOS_drive.d,
        0x1280 + idx, 0x02, &COB_ID, sizeof(COB_ID), 0);
    if (result != OD_SUCCESSFUL)
        return 0;
    
    // node ID
    result = writeLocalDict (EPOS_drive.d,
        0x1280 + idx, 0x03, &slaveid, sizeof(slaveid), 0);
    if (result != OD_SUCCESSFUL)
        return 0;
    
    return 1;
}

/*
 * Name         : epos_setup_rx_pdo
 *
 * Synopsis     : int     epos_setup_rx_pdo (UNS8 slaveid, int idx)
 *
 * Arguments    : UNS8  slaveid : slave ID
 *                int  idx : index of the slave in the slave table (for determining the location)
 *
 * Description  : sets up the RX PDOs for the slave, and disables them
 * 
 * Returns      : int     
 */
int     epos_setup_rx_pdo (UNS8 slaveid, int idx) {
    
    int     pdonr;

    UNS32   cobs[EPOS_PDO_MAX] = {0x180, 0x280, 0x380, 0x480};
    
    UNS32   result;
    UNS32   COB_ID;
    UNS8    trans_type = 0xFF;
    UNS8    map_count = 0x00;
    
    for (pdonr = 0; pdonr < EPOS_PDO_MAX; pdonr++) {

        // the PDO params
        COB_ID = 0x80000000 + cobs[pdonr] + slaveid;
        result = writeLocalDict (EPOS_drive.d,
            0x1400 + pdonr + (idx * EPOS_PDO_MAX), 0x01, &COB_ID, sizeof(COB_ID), 0);
        if (result != OD_SUCCESSFUL)
            return 0;

        result = writeLocalDict (EPOS_drive.d,
            0x1400 + pdonr + (idx * EPOS_PDO_MAX), 0x02, &trans_type, sizeof(trans_type), 0);
        if (result != OD_SUCCESSFUL)
            return 0;
        
        // setup the PDO mapping
        result = writeLocalDict (EPOS_drive.d,
            0x1600 + pdonr + (idx * EPOS_PDO_MAX), 0x00, &map_count, sizeof(map_count), 0);
        if (result != OD_SUCCESSFUL)
            return 0;        
    }
    
    return 1;
}


/*
 * Name         : epos_setup_tx_pdo
 *
 * Synopsis     : int     epos_setup_tx_pdo (UNS8 slaveid, int idx)
 *
 * Arguments    : UNS8  slaveid : slave ID
 *                int  idx : index of the slave in the slave table (for determining the location)
 *
 * Description  : sets up the TX PDOs for the slave, and disables them
 * 
 * Returns      : int     
 */

int     epos_setup_tx_pdo (UNS8 slaveid, int idx) {
    
    int     pdonr;

    UNS32   cobs[EPOS_PDO_MAX] = {0x200, 0x300, 0x400, 0x500};
    
    UNS32   result;
    UNS32   COB_ID;
    UNS8    trans_type = 0xFF;
    UNS8    map_count = 0x00;
    UNS16   inhibit_time = 10; //(it's in 100us, 10 = 1ms)
    
    for (pdonr = 0; pdonr < EPOS_PDO_MAX; pdonr++) {

        // the PDO params
        COB_ID = 0x80000000 + cobs[pdonr] + slaveid;
        result = writeLocalDict (EPOS_drive.d,
            0x1800 + pdonr + (idx * EPOS_PDO_MAX), 0x01, &COB_ID, sizeof(COB_ID), 0);
        if (result != OD_SUCCESSFUL)
            return 0;

        result = writeLocalDict (EPOS_drive.d,
            0x1800 + pdonr + (idx * EPOS_PDO_MAX), 0x02, &trans_type, sizeof(trans_type), 0);
        if (result != OD_SUCCESSFUL)
            return 0;
        
        result = writeLocalDict (EPOS_drive.d,
            0x1800 + pdonr + (idx * EPOS_PDO_MAX), 0x03, &inhibit_time, sizeof(inhibit_time), 0);
        if (result != OD_SUCCESSFUL)
            return 0;
                
        // setup the PDO mapping
        result = writeLocalDict (EPOS_drive.d,
            0x1A00 + pdonr + (idx * EPOS_PDO_MAX), 0x00, &map_count, sizeof(map_count), 0);
        if (result != OD_SUCCESSFUL)
            return 0;        
    }
    
    return 1;
}

/*
 * Name         : epos_setup_master
 *
 * Synopsis     : int     epos_setup_master ()
 *
 * Description  : set ups the master
 * 
 * Returns      : int     
 */

int     epos_initialize_master (CO_Data * d, const char * dcf_file) {
    
    int idx;
    
    EPOS_drive.epos_slave_count = 0;
    EPOS_drive.d = d;
    
    for (idx = 0; idx < MAX_EPOS_DRIVES; idx++) {
        // clean the slaves
        epos_slaves[idx] = 0x00;
        
        // clean the DCF data
        dcf_data[idx][0] = 0x00;
        dcf_data[idx][1] = 0x00;
        dcf_data[idx][2] = 0x00;
        dcf_data[idx][3] = 0x00;
        
        // clean the error data
        slave_err[idx][0] = 0x00;

        // set the callbacks
        
        // callback for drive status word
        RegisterSetODentryCallBack (d, 0x5041, 0x01 + idx, _statusWordCB);
    }
    
}

/*
 * Name         : epos_get_slave_index
 *
 * Synopsis     : int     epos_get_slave_index (UNS8 slaveid)
 *
 * Arguments    : UNS8  slaveid : Slave ID
 *
 * Description  : returns the slave index for the provided ID
 * 
 * Returns      : int     
 */

int     epos_get_slave_index (UNS8 slaveid) {
    
    int idx;
    for (idx = 0; idx < EPOS_drive.epos_slave_count; idx++)
        if (EPOS_drive.epos_slaves[idx] == slaveid)
            return idx;
        
    return -1;
}

void    _statusWordCB (CO_Data * d, const indextable *idx, UNS8 bSubindex) {
    
    // idx is the OD entry, bSubindex is the array item in it (eq. drive idx + 1)
    int     idx = bSubindex - 1;

    // update the state for the corresponding drive based on the status word
    EPOS_drive.EPOS_State[idx] = (*(UNS16 *)idx->pSubindex[bSubindex].pObject) & 0x417F;
    
    /***** NOTE: callback from PDO, NO MUTEXES! ****/
    switch (EPOS_State) {
        case EPOS_START:
            if (debug) eprintf("Start\n");
            break;
        case EPOS_NOTREADY:
            if (debug) eprintf("Not Ready to Switch On\n");
            // transition 1 to switch on disabled
            // AUTOMATIC
            break;
        case EPOS_SOD:
            if (debug) eprintf("Switch On Disabled\n");
            // transition 2 to ready to switch on
            /* should be done if we REQUESTED to turn on */
            //EnterMutex();
            SET_BIT(ControlWord, 2);
            SET_BIT(ControlWord, 1);
            CLEAR_BIT(ControlWord, 0);              
            //LeaveMutex();
            break;
        case EPOS_RSO:
            if (debug) eprintf("Ready to Switch On\n");
            // transition 3 to switched on
            // transition 7 to switch on disabled
            /* 3 if requested to turn on, 7 if requested to shut down */
            // this is #3
            //EnterMutex();
            SET_BIT(ControlWord, 2);
            SET_BIT(ControlWord, 1);
            SET_BIT(ControlWord, 0);
            //LeaveMutex();
            break;
        case EPOS_SWO:
            if (debug) eprintf("Switched on\n");
            // transition 4 to refresh -> measure
            // transition 6 to ready to switch on
            // transition 10 to switch on disabled
            /* 4 if requested to turn on, 6 or 10 if requested to shut down */
            // this is #4
            //EnterMutex();
            SET_BIT(ControlWord, 3);
            SET_BIT(ControlWord, 2);
            SET_BIT(ControlWord, 1);
            SET_BIT(ControlWord, 0);
            //LeaveMutex();
            break;
        case EPOS_REFRESH:
            if (debug) eprintf("Refresh\n");
            // transition 20 to measure
            // this should be automatic
            break;
        case EPOS_MEASURE:
            if (debug) eprintf("Measure Init\n");
            // transition 21 to operation enable
            // this should be automatic
            break;
        case EPOS_OPEN:
            if (debug) eprintf("Operation enable\n");
            // transition 5 to switched on
            // transition 8 to readdy to switch on
            // transition 9 to switch on disabled
            // transition 11 to quick stop active
            break;
        case EPOS_QUICKS:
            if (debug) eprintf("Quick Stop Active\n");
            // transition 16 to operation enable
            // transition 12 to switch on disabled
            break;
        case EPOS_FRAD:
            if (debug) eprintf("Fault Reaction Active (disabled)\n");
            // transition 18 to fault
            break;
        case EPOS_FRAE:
            if (debug) eprintf("Fault Reaction Active (enabled)\n");
            // transition 14 to fault
            break;
        case EPOS_FAULT:
            if (debug) eprintf("Fault\n");
            // transition 15 to switch on disabled
            break;
        default:
            eprintf("Bored to input codes. Unknown code %04x\n", EPOS_State);
    }

    // do the stupid assuming of position, and execution
    // if the set point ack is SET, then CLEAR the new set point flag
    if(BIT_IS_SET(StatusWord, 12)) {
        if (BIT_IS_SET(ControlWord, 4)) {
            CLEAR_BIT(ControlWord, 4);
            if (debug) eprintf ("New move ACK!\n");
        } else {
            eprintf ("What the FUCKING hell???\n");
        }
    }
    
    sendPDOevent(d);
}
