#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "fusb302b.h"
#include "pd.h"
#include "pd_com.h"
// FIXME: do not include the kitchen sink here
#include "sysctl.h"

void print_src_fixed_pdo(int number, uint32_t pdo)
{
    unsigned int tmp;

    printf("[pd_src_fixed_pdo]\n");
    printf("\tnumber = %d\n", number);

    /* Dual-role power */
    tmp = (pdo & PD_PDO_SRC_FIXED_DUAL_ROLE_PWR) >> PD_PDO_SRC_FIXED_DUAL_ROLE_PWR_SHIFT;
    if (tmp)
    {
        printf("\tdual_role_pwr = %d\n", tmp);
    }

    /* USB Suspend Supported */
    tmp = (pdo & PD_PDO_SRC_FIXED_USB_SUSPEND) >> PD_PDO_SRC_FIXED_USB_SUSPEND_SHIFT;
    if (tmp)
    {
        printf("\tusb_suspend = %d\n", tmp);
    }

    /* Unconstrained Power */
    tmp = (pdo & PD_PDO_SRC_FIXED_UNCONSTRAINED) >> PD_PDO_SRC_FIXED_UNCONSTRAINED_SHIFT;
    if (tmp)
    {
        printf("\tunconstrained_pwr = %d\n", tmp);
    }

    /* USB Communications Capable */
    tmp = (pdo & PD_PDO_SRC_FIXED_USB_COMMS) >> PD_PDO_SRC_FIXED_USB_COMMS_SHIFT;
    if (tmp)
    {
        printf("\tusb_comms = %d\n", tmp);
    }

    /* Dual-Role Data */
    tmp = (pdo & PD_PDO_SRC_FIXED_DUAL_ROLE_DATA) >> PD_PDO_SRC_FIXED_DUAL_ROLE_DATA_SHIFT;
    if (tmp)
    {
        printf("\tdual_role_data = %d\n", tmp);
    }

    /* Unchunked Extended Messages Supported */
    tmp = (pdo & PD_PDO_SRC_FIXED_UNCHUNKED_EXT_MSG) >> PD_PDO_SRC_FIXED_UNCHUNKED_EXT_MSG_SHIFT;
    if (tmp)
    {
        printf("\tunchunked_ext_msg = %d\n", tmp);
    }

    /* Peak Current */
    tmp = (pdo & PD_PDO_SRC_FIXED_PEAK_CURRENT) >> PD_PDO_SRC_FIXED_PEAK_CURRENT_SHIFT;
    if (tmp)
    {
        printf("\tpeak_i = %d\n", tmp);
    }

    /* Voltage */
    tmp = (pdo & PD_PDO_SRC_FIXED_VOLTAGE) >> PD_PDO_SRC_FIXED_VOLTAGE_SHIFT;
    printf("\tv = %d.%02d\n", PD_PDV_V(tmp), PD_PDV_CV(tmp));

    /* Maximum Current */
    tmp = (pdo & PD_PDO_SRC_FIXED_CURRENT) >> PD_PDO_SRC_FIXED_CURRENT_SHIFT;
    printf("\ti_a: %d.%02d\n", PD_PDI_A(tmp), PD_PDI_CA(tmp));
}

unsigned int t = 0;

unsigned int pd_state;
bool pd_sent_soft_reset;
uint16_t pd_datarole = PD_DATAROLE_UFP;
uint16_t pd_powerrole = PD_POWERROLE_SINK;
static bool pd_datarole_changed = false;

int request_sent = 0;

union pd_msg tx;
int tx_id_count = 0;
union pd_msg rx_msg;
unsigned int ccpin = 0;
// in 10mA units
int requested_current = 0;

int factory_turn_on_once = 1;

void pd_init() {
  pd_state = PD_STATE_SETUP;
  pd_sent_soft_reset = 0;
}

unsigned int pd_get_state_for_debug() {
  return pd_state;
}

inline void pd_set_fusb_switches1() {
  fusb_write_byte(FUSB_SWITCHES1, FUSB_SWITCHES1_SPECREV_REV2_0| (pd_datarole == PD_DATAROLE_DFP) ? FUSB_SWITCHES1_DATAROLE_SRC_DFP : FUSB_SWITCHES1_DATAROLE_SNK_UFP);
}

bool pd_tick(battery_info_s* battery_info) {
  if (pd_state == PD_STATE_SETUP) {
    // setup/timeout state
    charger_disable_charge();
    request_sent = 0;

    printf("# [pd] PD_STATE_SETUP\n");
    // probe FUSB302BMPX
    uint8_t rxdata[2];
    if (i2c_read_timeout_us(i2c0, FUSB_ADDR, rxdata, 1, false, I2C_TIMEOUT)) {
      //printf("# [pd] FUSB probed.\n");
      // SW_RES: Reset the FUSB302B including the I2C registers to their default values
      fusb_write_byte(FUSB_RESET, FUSB_RESET_SW_RES);

      sleep_us(100);

      // enable toggle and DRP mode
      int mode;
      if (battery_info->som_is_powered) {
        // enable 5V for host mode
        usb_host_5v_enable();
        mode = 1 << FUSB_CONTROL2_MODE_SHIFT;  // DRP
      } else {
        usb_host_5v_disable();
        mode = 0b10 << FUSB_CONTROL2_MODE_SHIFT;  // SNK only
      }
      mps_reg_config.config0.susp_en = 1;
      mps_reg_config.config0.chg_en = 0;
      mps_write_byte(MPS_REG_CONFIG0, mps_reg_config.config0.reg_byte);

      fusb_write_byte(FUSB_CONTROL2, FUSB_CONTROL2_TOGGLE | mode);

      fusb_write_byte(FUSB_MASK1, 0); //0xEE);  // enable I_VBUSOK
      fusb_write_byte(FUSB_MASKA, 0); //0xBF);  // enable I_TOGDONE
      fusb_write_byte(FUSB_MASKB, 0x01);

      // state is now:
      // Switches0 PDWN1=1 PDWN2=1
      // Switches1 SpecRev=Rev2.0
      // Measure MDAC=? 11_0001 =?
      // Slice SDAC_HYS=11 255mV
      // Control0 HOST_CUR=01 host pull up 80uA (Default) ; INT_MASK=1 All Interrupts masked
      // Control1 all zero
      // Control2 MODE=01 Enable DRP polling functionality if TOGGLE=1
      // Control3 N_RETRIES=11 (Three)   - AUTO_HARDRESET=0 AUTO_SOFTRESET=0
      // Mask all zero
      // Power PWR0=1
      // Reset 0
      // OCPReg OCP_CUR=111 max range ; OCP_RANGE=1 OCP range between 100−800 mA
      // MaskA all zero
      // MaskB all zero
      // Control4 all zero
      // Status0a all zero
      // Status1a all zero
      // InterruptA
      // InterruptB
      // Status0 all zero
      // Status1 all zero
      // Interrupt
      // FIFOs

      // disable interrupt mask, flush tx buffer
      fusb_write_byte(FUSB_CONTROL0, FUSB_CONTROL0_HOST_CUR | FUSB_CONTROL0_TX_FLUSH);
      // flush rx buffer
      fusb_write_byte(FUSB_CONTROL1, FUSB_CONTROL1_RX_FLUSH);

      // turn on low power
      fusb_write_byte(FUSB_POWER, 0x01);
      // automatic retransmission + auto hard+soft reset
      fusb_write_byte(FUSB_CONTROL3,
                      FUSB_CONTROL3_AUTO_HARDRESET |
                      FUSB_CONTROL3_AUTO_SOFTRESET |
                      (3<<FUSB_CONTROL3_N_RETRIES_SHIFT) |
                      FUSB_CONTROL3_AUTO_RETRY |
                      FUSB_CONTROL3_SEND_HARD_RESET
                      );

      printf("# [pd] PD_STATE_SETUP done, going to PD_STATE_UNATTACHED\n");

      t = 0;
      pd_state = PD_STATE_UNATTACHED; // setup done

    } else {
      if (t > 1000) {
        printf("# [pd] PD_STATE_SETUP: fusb timeout.\n");
        t = 0;
      }
    }

  } else if (pd_state == PD_STATE_UNATTACHED) {
    // setup done, wait for attach irq
    pd_sent_soft_reset = false;

    // read and clear all interrupts - FIXME: do it in one I2C transaction
    int i_irq = fusb_read_byte(FUSB_INTERRUPT);
    int i_irqa = fusb_read_byte(FUSB_INTERRUPTA);
    int i_irqb = fusb_read_byte(FUSB_INTERRUPTB);
    (void)i_irqb;
    (void)i_irq;

    //printf("# [pd] PD_STATE_UNATTACHED FUSB_INTERRUPT = 0x%02x FUSB_INTERRUPTA = 0x%02x FUSB_INTERRUPTB = 0x%02x\n", i_irq, i_irqa, i_irqb);
    if (i_irqa & FUSB_INTERRUPTA_I_TOGDONE) {
      int togss = (fusb_read_byte(FUSB_STATUS1A) & FUSB_STATUS1A_TOGSS) >> FUSB_STATUS1A_TOGSS_SHIFT;
      if (togss == 5) {
        // SNK CC1
        printf("# [pd] PD_STATE_UNATTACHED -> SNK CC1, going to PD_STATE_UNATTACHED_SNK\n");
        pd_state = PD_STATE_UNATTACHED_SNK;
        ccpin = 1;
      } else if (togss == 6) {
        // SNK CC2
        printf("# [pd] PD_STATE_UNATTACHED -> SNK CC2, going to PD_STATE_UNATTACHED_SNK\n");
        pd_state = PD_STATE_UNATTACHED_SNK;
        ccpin = 2;
      } else if (togss == 1 || togss == 2) {
        // SRC ...
        // FIXME: store ccpin
        // TODO: test (and then implement) this with an actual PD-speaking device.
        printf("# [pd] PD_STATE_UNATTACHED -> SRC, going to PD_STATE_UNATTACHED_SRC\n");
        pd_state = PD_STATE_UNATTACHED_SRC;
      }

      if (pd_state == PD_STATE_UNATTACHED_SNK) {
        pd_powerrole = PD_POWERROLE_SINK;
        pd_datarole = PD_DATAROLE_UFP;  // default for powerrole SINK
        usb_host_5v_disable();
        sleep_us(10); // TODO: get rid of this?

        fusb_write_byte(FUSB_POWER, 0x7);

        // unattached.snk: Host software enables FUSB302B pull−downs and measure block to detect attach
        if (ccpin == 1) {
          fusb_write_byte(FUSB_SWITCHES0, FUSB_SWITCHES0_MEAS_CC1|FUSB_SWITCHES0_PDWN_2|FUSB_SWITCHES0_PDWN_1);
        } else if (ccpin == 2) {
          fusb_write_byte(FUSB_SWITCHES0, FUSB_SWITCHES0_MEAS_CC2|FUSB_SWITCHES0_PDWN_2|FUSB_SWITCHES0_PDWN_1);
        }
        pd_set_fusb_switches1();
        pd_datarole_changed = false;

        // enable vbus measure for attach
        int v = (3670 / 420) - 1;
        uint8_t measure = FUSB_MEASURE_MEAS_VBUS | v;
        fusb_write_byte(FUSB_MEASURE, measure);

        // disable TOGGLE
        fusb_write_byte(FUSB_CONTROL2, (1 << FUSB_CONTROL2_MODE_SHIFT));

        printf("[pd] PD_STATE_UNATTACHED_SNK enable pulldown, measure, power; measure=0x%02x\n", measure);
      } else {
        pd_powerrole = PD_POWERROLE_SOURCE;
        pd_datarole = PD_DATAROLE_DFP;  // default for powerrole SOURCE
        usb_host_5v_enable();
        fusb_write_byte(FUSB_POWER, 0xF);
        fusb_write_byte(FUSB_SWITCHES0, FUSB_SWITCHES0_PU_EN1|FUSB_SWITCHES0_PU_EN2);
        pd_set_fusb_switches1();
        pd_datarole_changed = false;
      }

      t = 0;
    }
  } else if (pd_state == PD_STATE_UNATTACHED_SNK) {
    // unattached.snk

    // Per FUSB302B docs, I_VBUSOK interrupt alerts host software that an attach has occurred.
    // However, we cannot use it, as in DRP mode I_VBUSOK was already triggered earlier - from our own VUSB feed!
    // Instead, we measure VBUS ourselves using the COMP block, and thus wait for I_COMP_CHNG.

    // read and clear all interrupts - FIXME: do it in one I2C transaction
    int i_irq = fusb_read_byte(FUSB_INTERRUPT);
    int i_irqa = fusb_read_byte(FUSB_INTERRUPTA);
    int i_irqb = fusb_read_byte(FUSB_INTERRUPTB);
    (void)i_irqb;
    (void)i_irqa;

    // printf("# [pd] PD_STATE_UNATTACHED_SNK FUSB_INTERRUPT = 0x%02x FUSB_INTERRUPTA = 0x%02x FUSB_INTERRUPTB = 0x%02x\n", i_irq, i_irqa, i_irqb);

    if (i_irq & FUSB_INTERRUPT_I_COMP_CHNG) {
      // Attached.SNK
      // Host software uses FUSB302B comparators and DAC to determine attach orientation and port type

      /* Measure CC1 */
      fusb_write_byte(FUSB_SWITCHES0, FUSB_SWITCHES0_MEAS_CC1|FUSB_SWITCHES0_PDWN_2|FUSB_SWITCHES0_PDWN_1);
      sleep_us(250);
      uint8_t cc1 = fusb_read_byte(FUSB_STATUS0) & FUSB_STATUS0_BC_LVL;

      /* Measure CC2 */
      fusb_write_byte(FUSB_SWITCHES0, FUSB_SWITCHES0_MEAS_CC2|FUSB_SWITCHES0_PDWN_2|FUSB_SWITCHES0_PDWN_1);
      sleep_us(250);
      uint8_t cc2 = fusb_read_byte(FUSB_STATUS0) & FUSB_STATUS0_BC_LVL;

      // detect orientation
      if (cc1 > cc2) {
        printf("# [pd] PD_STATE_UNATTACHED_SNK using cc1 to go to PD_STATE_ATTACHED_SNK\n");
        fusb_write_byte(FUSB_SWITCHES1, FUSB_SWITCHES1_AUTO_CRC|FUSB_SWITCHES1_TXCC1);
        fusb_write_byte(FUSB_SWITCHES0, FUSB_SWITCHES0_MEAS_CC1|FUSB_SWITCHES0_PDWN_2|FUSB_SWITCHES0_PDWN_1);
      } else {
        printf("# [pd] PD_STATE_UNATTACHED_SNK using cc2 to go to PD_STATE_ATTACHED_SNK\n");
        fusb_write_byte(FUSB_SWITCHES1, FUSB_SWITCHES1_AUTO_CRC|FUSB_SWITCHES1_TXCC2);
        fusb_write_byte(FUSB_SWITCHES0, FUSB_SWITCHES0_MEAS_CC2|FUSB_SWITCHES0_PDWN_2|FUSB_SWITCHES0_PDWN_1);
      }

      fusb_write_byte(FUSB_POWER, 0xF);

      t = 0;
      pd_state = PD_STATE_ATTACHED_SNK;

    } else if (t>10000) {
      // timeout, assume unattached.src
      // FIXME: timeout value?
      printf("# [pd] state PD_STATE_UNATTACHED_SNK - timeout\n");
      t = 0;
      pd_state = PD_STATE_SETUP; // FIXME: what state should we go to?
    }
  } else if (pd_state == PD_STATE_ATTACHED_SNK) {
    // attached.snk.
    // need to handshake charging capability and wait for ps_ok
    int irq = fusb_read_byte(FUSB_INTERRUPT);
    if (irq & FUSB_INTERRUPT_I_VBUSOK) {
      t = 0;
      pd_state = PD_STATE_SETUP;
      printf("# [pd] state PD_STATE_ATTACHED_SNK FUSB_INTERRUPT_I_VBUSOK detach \n");
    } else {
      if (pd_datarole_changed) {
        pd_set_fusb_switches1();
        pd_datarole_changed = false;
      }
      // FIXME: this does not enforce the proper message order. maybe ok as is, maybe not.
      if (fusb_read_message(&rx_msg)) {
        uint8_t msgtype = PD_MSGTYPE_GET(&rx_msg);
        uint8_t numobj = PD_NUMOBJ_GET(&rx_msg);
        uint8_t msgrole = PD_POWERROLE_GET(&rx_msg);
        printf("# [pd] PD_STATE_ATTACHED_SNK: charger responds msg type: 0x%x msgrole: %d numobj: %d\n", msgtype, msgrole, numobj);
        if (msgrole == PD_POWERROLE_SOURCE && msgtype == PD_MSGTYPE_SOURCE_CAPABILITIES) {
          if (numobj == 0) {
            // FIXME: trigger a reset without sending a message first / or send reject?
            // TODO: figure out if this is actually caused by an overrun of the FUSB RX FIFO
            //usbpd_state = PD_STATE_SETUP;
          } else {
            int max_voltage = 0;
            int power_objects = 0;
            int pdo_current = 0;
            for (int i=0; i<numobj; i++) {
              uint32_t pdo = rx_msg.obj[i];

              if ((pdo & PD_PDO_TYPE) == PD_PDO_TYPE_FIXED) {
                print_src_fixed_pdo(i + 1, pdo);
                int voltage = PD_PDV_V(PD_PDO_SRC_FIXED_VOLTAGE_GET(pdo));
                // PD reports power in 10mA steps
                int current = PD_PDO_SRC_FIXED_CURRENT_GET(pdo);
                if (voltage > max_voltage && voltage <= 20 && current >= 10) {
                  power_objects = i+1;
                  max_voltage = voltage;
                  pdo_current = current;
                }
              } else {
                printf("# [pd] PD_STATE_ATTACHED_SNK not a fixed PDO: 0x%08lx\n", pdo);
              }
            }

            printf("# [pd] discarding further messages\n");
            while (fusb_read_message(&rx_msg)) {
              // TODO: can probably remove this
            }

            // FIXME: what about headroom for passing power to other USB devices?
            requested_current = pdo_current;
            if (requested_current > 300) {
              requested_current = 300;
            }

            printf("# [pd] requesting PO %d, %d V at %d mA\n", power_objects, max_voltage, requested_current * 10);
            tx.hdr = PD_MSGTYPE_REQUEST | PD_NUMOBJ(1) | pd_datarole | (pd_powerrole << PD_HDR_POWERROLE_SHIFT) | PD_SPECREV_2_0;

            tx.hdr &= ~PD_HDR_MESSAGEID;
            tx.hdr |= (tx_id_count % 8) << PD_HDR_MESSAGEID_SHIFT;

            tx.obj[0] = PD_RDO_FV_MAX_CURRENT_SET(requested_current)
              | PD_RDO_FV_CURRENT_SET(requested_current)
              | PD_RDO_USB_COMMS
              | PD_RDO_NO_USB_SUSPEND
              | PD_RDO_OBJPOS_SET(power_objects);

            fusb_send_message(&tx);

            tx_id_count++;
          }
          t = 0;
        } else if (msgrole == PD_POWERROLE_SOURCE && msgtype == PD_MSGTYPE_ACCEPT) {
          printf("# [pd] charger accepted our requested PDO.\n");
          t = 0;
        } else if (msgrole == PD_POWERROLE_SOURCE && msgtype == PD_MSGTYPE_PS_RDY) {
          // power supply is ready
          printf("# [pd] power supply ready.\n");

          charger_enable_charge(requested_current);

          t = 0;
        } else if (msgrole == PD_POWERROLE_SOURCE && msgtype == PD_MSGTYPE_DR_SWAP) {
          // other side wants to swap data role.
          if (pd_datarole == PD_DATAROLE_DFP) {
            // we cannot switch away from DFP role. reject the message
            printf("# [pd] rejecting data-role swap\n");
            tx.hdr = PD_MSGTYPE_REJECT | pd_datarole | (pd_powerrole << PD_HDR_POWERROLE_SHIFT);
            fusb_send_message(&tx);
          } else {
            // we started as UFP. Partner wants to become UFP.
            if (!battery_info->som_is_powered) {
              // SOM is not powered, so it will not act as a host. Tell partner to try later.
              printf("# [pd] replying with wait to data-role swap request\n");
              tx.hdr = PD_MSGTYPE_WAIT | pd_datarole | (pd_powerrole << PD_HDR_POWERROLE_SHIFT);
              fusb_send_message(&tx);
            } else {
              // Accept. We become the DFP (host).
              printf("# [pd] accepting data-role swap\n");
              // TODO: switch pd_dr_role only after GOOD_CRC
              tx.hdr = PD_MSGTYPE_ACCEPT | pd_datarole | (pd_powerrole << PD_HDR_POWERROLE_SHIFT);
              fusb_send_message(&tx);
              pd_datarole = PD_DATAROLE_DFP;
              pd_datarole_changed = true;
            }
          }
          t = 0;
        } else if (msgrole != PD_POWERROLE_SOURCE) {
          printf("# [pd] discarding non-source msg type: 0x%x numobj: %d\n", msgtype, numobj);
          t = 0;
        } else {
          printf("# [pd] msg type: 0x%x numobj: %d\n", msgtype, numobj);
          tx.hdr = PD_MSGTYPE_REJECT | pd_datarole | (pd_powerrole << PD_HDR_POWERROLE_SHIFT);
          fusb_send_message(&tx);
        }
      } else if (t>10000 && !mps_reg_config.config0.chg_en) {
        // for some reason charging did not start.
        // TODO: send soft reset first.
        // TODO: fix timer.
        printf("# [pd] PD_STATE_ATTACHED_SNK timeout while handshaking, reset\n");
        t = 0;
        pd_state = PD_STATE_SETUP;
      } else if (t>8000 && !mps_reg_config.config0.chg_en && !pd_sent_soft_reset) {
        // Charging did not start.
        // This situation was observed with an Apple 30W charger, which apparently ignores a hard-reset
        // without a soft-reset and without an actual detach. Unclear why this happens.
        // Necessary to handle this so charging resumes after sysctl gets rebooted by a firmware upgrade.
        // TODO: the usbpd_sent_soft_reset stuff is not great.
        // TODO: fix timer.
        pd_sent_soft_reset = true;
        printf("# [pd] PD_STATE_ATTACHED_SNK timeout while handshaking, sending soft reset\n");
        tx.hdr = PD_MSGTYPE_SOFT_RESET | pd_datarole | (pd_powerrole << PD_HDR_POWERROLE_SHIFT);
        fusb_send_message(&tx);
      }
    }

#ifdef FACTORY_MODE
    // in factory mode, turn on power immediately to be able to flash
    // the keyboard
    if (factory_turn_on_once) {
      factory_turn_on_once = 0;
      turn_som_power_on();
    }
#endif
  } else if (pd_state == PD_STATE_UNATTACHED_SRC) {
    // TODO: should do a lot of stuff
    fusb_write_byte(FUSB_POWER, 0xF);

    t = 0;
    pd_state = PD_STATE_ATTACHED_SRC;
  } else if (pd_state == PD_STATE_ATTACHED_SRC) {
    // TODO: everything
    // TODO: timeout

    pd_state = PD_STATE_SETUP;
  }

  bool can_sleep = t > 0;
  t++;

  return can_sleep;
}
