1. The GUI should have HMI and multitab sreens layout:
- `Main` tab: Progtrammer dashboard.
- `EE_CUST0` tab: To display parameters values of EEPROM (0x09) and Shadow Register (0x19).
- `EE_CUST1` tab: To display parameters values of EEPROM (0x0A) and shadow register (0x1A)
- `EE_CUST2` tab: To display value of EEPROM (0x08).
- `FAULT_STATUS` tab: To display parameters of Volatile Register (0x20)


2. HMI should have the following control buttons and status indicators located on the `Main` tab:
    - *Communication Status Bar Indicator*: If the status bar is red, the communication is not active and if green, the application is communicating with the ASC37610 programmer.
    - *Power Status Bar Indicator* ; If the status bar is red, DUT on the programmer is not powered up and if green, the DUT on the programmer is powered up.
    - *Power On* and *Power Off* button: If click *Power On*, the PWR-EN signal on the programmer should go low, which does enable 3.3V power rail to power up DUT. 
    - *Power Off* button: If click *Power Off*, the PWR-EN signal on the programer should go high.
    - *Read All* button: If click *Read All* programmer initiating reading sequency from all 4 EEPROM loactions 
    - *Read All Status Bar Indicator*: If the status bar is green, the `read all` is completed and if red, its faild (due to not recieving respond or CRC error).

3. All other tabs should have at minimum *Read* and *Write* buttons with corresponding status bars. Since Volative Register read only, the `FAULT_STATUS` tab should have only *Read* button.

4. User should be able to read all regsiters and save values into file on the host PC.

5. User should be able load all registers values from the file saved on host PC into programmer for review. 

6. User schould be able to edit current parameter values of corresponding register.

7. User should be able to write updated parametrs value into selected register by clicking *Write* buitton on corresponding `Tab` 

8. Programmer should read back written value after each write command and compare with expected values. If values are identical set corresponding status bar green (`Completed`), otherwise - red (`Fail`).