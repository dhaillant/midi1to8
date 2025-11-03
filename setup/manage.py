import sys
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QVBoxLayout, QHBoxLayout, QGridLayout,
    QWidget, QLabel, QComboBox, QCheckBox, QPushButton, QMessageBox
)
from PyQt5.QtGui import QPixmap
from PyQt5.QtCore import QSettings
from mido import MidiFile, MidiTrack, Message, get_output_names, open_output, get_input_names, open_input

SYSEX_START   = 0xF0
SYSEX_END     = 0xF7

MANUFACTURER  = 0x7D	# non official ID for experiments and DIY 
MODEL         = 0x18	# MIDI 1-8 ("0x8d" for MIDI8d, etc...)
DEVICE        = 0x01	# first device (could be set by config for managing several identical chained devices)

COMMAND_PING_DEVICE      = 0x01
COMMAND_READ_FROM_DEVICE = 0x02
COMMAND_WRITE_TO_DEVICE  = 0x03
COMMAND_CHANGE_DEVICE_ID = 0x04


class MidiApp(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("MIDI 1-8 Setup")
        self.resize(400, 300)

        # Initialize QSettings for storing/retrieving configurations of the app
        self.settings = QSettings("David Haillant", "MIDI 1-8")
        print("Reading settings from:")
        print(self.settings.fileName())

        # Initialize checkbox states (2D array: 8 outputs × 17 channels)
        self.checkbox_states = [[False for _ in range(17)] for _ in range(8)]

        # left picture with front panel
        side_picture = QLabel()
        side_picture.setPixmap(QPixmap('frontpanel_62x400.png'))

		# right panel, will contain controls
        main_vbox_layout = QVBoxLayout()

        # main horizontal layout, frontpanel on left, controls on right
        main_hbox_layout = QHBoxLayout()
        main_hbox_layout.addWidget(side_picture)
        main_hbox_layout.addLayout(main_vbox_layout)


        ### Control panel ###

        # First ROW: MIDI Output Device Selection
        midi_output_device_layout = QHBoxLayout()
        self.midi_output_device_label = QLabel("MIDI Output Device:")
        self.midi_output_device_dropdown = QComboBox()

        #self.midi_output_device_dropdown.addItems(get_output_names())
        self.midi_output_device_dropdown_items = get_output_names()
        self.midi_output_device_dropdown.addItems(self.midi_output_device_dropdown_items)

        midi_output_device_layout.addWidget(self.midi_output_device_label)
        midi_output_device_layout.addWidget(self.midi_output_device_dropdown)
        main_vbox_layout.addLayout(midi_output_device_layout)

        # --------------------------------------------

        # second ROW: MIDI Input Device Selection
        midi_input_device_layout = QHBoxLayout()
        self.midi_input_device_label = QLabel("MIDI Input Device:")
        self.midi_input_device_dropdown = QComboBox()

        #self.midi_input_device_dropdown.addItems(get_input_names())
        self.midi_input_device_dropdown_items = get_input_names()
        self.midi_input_device_dropdown.addItems(self.midi_input_device_dropdown_items)

        midi_input_device_layout.addWidget(self.midi_input_device_label)
        midi_input_device_layout.addWidget(self.midi_input_device_dropdown)
        main_vbox_layout.addLayout(midi_input_device_layout)

        # Restore previous selections
        saved_output = self.settings.value("midi_output_device")
        if saved_output in self.midi_output_device_dropdown_items:
            index = self.midi_output_device_dropdown.findText(saved_output)
            self.midi_output_device_dropdown.setCurrentIndex(index)

        saved_input = self.settings.value("midi_input_device")
        if saved_input in self.midi_input_device_dropdown_items:
            index = self.midi_input_device_dropdown.findText(saved_input)
            self.midi_input_device_dropdown.setCurrentIndex(index)

        # --------------------------------------------

        # Third ROW: Channel Checkboxes matrix in GRID
        self.output_matrix_grid_layout = QGridLayout()

        # Add column headers (1–16 and RT)
        self.output_matrix_grid_layout.addWidget(QLabel("Channels:"), 0, 0)
        for i in range(16):
            header = QLabel(str(i + 1))                     # MIDI channels are 1..16
            self.output_matrix_grid_layout.addWidget(header, 0, i + 1)      # first row of the grid, from the second colmun

        rt_header = QLabel("RT")
        self.output_matrix_grid_layout.addWidget(rt_header, 0, 17)          # first row of the grid, colmun 18


        # Populate the grid with outputs and checkboxes
        for row in range(8):
            # Add output name label
            output_label = QLabel(f"Output {row + 1}")
            self.output_matrix_grid_layout.addWidget(output_label, row + 1, 0)

            # Create checkboxes for 16 channels + RT
            for col in range(17):  # 16 channels + RT
                checkbox = QCheckBox()
                self.output_matrix_grid_layout.addWidget(checkbox, row + 1, col + 1)

                # Update state when checkbox is toggled
                checkbox.stateChanged.connect(
                    lambda state, r=row, c=col: self.update_checkbox_state(r, c, state)
                )

            # Add "All" and "None" buttons
            check_all_button = QPushButton("All")
            check_none_button = QPushButton("None")
            self.output_matrix_grid_layout.addWidget(check_all_button, row + 1, 18)
            self.output_matrix_grid_layout.addWidget(check_none_button, row + 1, 19)

            # Connect buttons to their respective functions
            check_all_button.clicked.connect(lambda _, r=row: self.toggle_row(r, True))
            check_none_button.clicked.connect(lambda _, r=row: self.toggle_row(r, False))

        main_vbox_layout.addStretch(1)


        '''
        self.channel_checkboxes = []
        channel_layout = QVBoxLayout()
        channel_label = QLabel("Enable Channels:")
        channel_layout.addWidget(channel_label)

        for i in range(16):
            checkbox = QCheckBox(f"Channel {i + 1}")
            checkbox.setChecked(True)  # Default all channels to enabled
            self.channel_checkboxes.append(checkbox)
            channel_layout.addWidget(checkbox)

        main_vbox_layout.addLayout(channel_layout)
        '''

        main_vbox_layout.addLayout(self.output_matrix_grid_layout)
        main_vbox_layout.addStretch(1)
        
        # Send Button
#        self.send_button = QPushButton("Send configuration")
#        #self.send_button.clicked.connect(self.send_midi_note)
#        self.send_button.clicked.connect(self.send_config)
#        main_vbox_layout.addWidget(self.send_button)

        # last row: retrieve and send buttons in horizontal layout
        receive_send_config_layout = QHBoxLayout()

        # retrieve from device button
        receive_button = QPushButton("Read configuration from device")
        receive_button.clicked.connect(self.read_config_from_device)
        receive_send_config_layout.addWidget(receive_button)

        # send to device button
        send_button = QPushButton("Send configuration to device")
        send_button.clicked.connect(self.write_config_to_device)
        receive_send_config_layout.addWidget(send_button)

        main_vbox_layout.addLayout(receive_send_config_layout)

        # Main widget
        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        main_widget.setLayout(main_hbox_layout)             # attach the frontpanel and the controls

    def update_checkbox_state(self, row, col, state):
        """Update the state of a checkbox in the 2D array."""
        self.checkbox_states[row][col] = state == 2  # 2 means "Checked"
        #print(f"check state [{row}][{col}] = {state}")

    def toggle_row(self, row, check_state):
        """Toggle all checkboxes in a row."""
        for col in range(17):  # 16 channels + RT
            checkbox = self.output_matrix_grid_layout.itemAtPosition(row + 1, col + 1).widget()
            checkbox.setChecked(check_state)
            self.checkbox_states[row][col] = check_state

    def send_midi_note(self):
        selected_device = self.midi_output_device_dropdown.currentText()
        #enabled_channels = [i + 1 for i, cb in enumerate(self.channel_checkboxes) if cb.isChecked()]

        '''
        if not enabled_channels:
            QMessageBox.warning(self, "No Channels Enabled", "Please enable at least one channel.")
            return
        '''

        try:
            with open_output(selected_device) as outport:
                for channel in range(16):
                    value = 0
                    #print("channel:")
                    for output in range(8):
                        value = (value << 1) + (self.checkbox_states[output][channel])
                    print("channel: ", channel, " ", format(value, 'b').zfill(8))
                        # Send a Note On message (Middle C) to the enabled channels
                        #outport.send(Message('note_on', note=60, velocity=64, channel=channel - 1))
                #QMessageBox.information(self, "Success", f"Sent note to channels: {enabled_channels}")
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to send MIDI note: {e}")

    def test_send_midi_sysex(self):
        # send SysEx [Mf. ID + Command ID + LED1 .. LED8 status]
        sysex_message = [0xF0, 0x7D, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF7]
        
        selected_device = self.midi_output_device_dropdown.currentText()
        try:
            with open_output(selected_device) as outport:
                outport.send(Message('sysex', data=sysex_message[1:-1]))  # Exclude F0 and F7
                print("SysEx message sent!")
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to send MIDI note: {e}")

    def read_config_from_device(self):
        # send a read request then listen for the answer
        print("retrieve data from device...")

        selected_input_device = self.midi_input_device_dropdown.currentText()
        selected_output_device = self.midi_output_device_dropdown.currentText()

        sysex_message = [SYSEX_START, MANUFACTURER, MODEL, DEVICE, COMMAND_READ_FROM_DEVICE]
        sysex_message.append(SYSEX_END)  # close the message
        print(" ".join(f"{byte:02X}" for byte in sysex_message))

        try:
            with open_output(selected_output_device) as outport:
                outport.send(Message('sysex', data=sysex_message[1:-1]))      # Exclude F0 and F7 as mido handles these internally for 'sysex' messages
                print("SysEx message sent!")
                #QMessageBox.information(self, "Success", f"Sent note to channels: {enabled_channels}")
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to send config through MIDI: {e}")


    def write_config_to_device(self):
        #manufacturer = 0x7D
        #model = 0x01
        #device = 0x01
        #command = 0x02
        sysex_message = [SYSEX_START, MANUFACTURER, MODEL, DEVICE, COMMAND_WRITE_TO_DEVICE]

        selected_device = self.midi_output_device_dropdown.currentText()
        '''
        for channel in range(16):
            enabled_outputs = 0
            #print("channel:")
            for output in range(8):
                # browse checkboxes and append enabled outputs (0 or 1) for this channel
                enabled_outputs = (enabled_outputs << 1) + (self.checkbox_states[output][channel])
            print("channel: ", channel, " ", format(enabled_outputs, 'b').zfill(8))
            sysex_message.append(enabled_outputs)
        '''

        '''
	# 24 bytes
        for output in range(8):
            enabled_channels = 0
            for channel in range(17):   # 16 channels + RT
                #enabled_channels = (enabled_channels << 1) + (self.checkbox_states[output][channel])
                if self.checkbox_states[output][channel]:
                    #enabled_channels |= (1 << (16 - channel))  # reverse order
                    enabled_channels |= (1 << channel)


            byte1 = ((enabled_channels >> (7 * 0)) & 0x7F)    # First 7 bits                           mask is 0x7F (0111 1111)
            byte2 = ((enabled_channels >> (7 * 1)) & 0x7F)    # Next 7 bits                            mask is 0x7F (0111 1111)
            byte3 = ((enabled_channels >> (7 * 2)) & 0x07)    # Remaining 3 bits (17 - 14 = 3), so the mask is 0x07 (0000 0111)

            print("output: ", output + 1, " ", format(enabled_channels, 'b').zfill(17))
            print("    byte 1: ", format(byte1, 'b').zfill(8))
            print("    byte 2: ", format(byte2, 'b').zfill(8))
            print("    byte 3: ", format(byte3, 'b').zfill(8))
            
            sysex_message += [byte1, byte2, byte3]
        '''

	# 20 bytes (17 + 17 MOD 7)
        '''
        for each channel,
            take only 7 lower outputs (bits) and carry the 8th bit
            append truncated byte to message
            if 7th channel (channel MOD 7),
                append the carry to the message
            end if
        end for each
        append last carry to message

        see https://www.echevarria.io/blog/midi-sysex/index.html

        However, in our case, we send MSB bits after the 7 truncated bytes

	Data to be sent: b1, b2, b3, b4, b5, b6, b7.

	MIDI output:
			<0 b1[0..6]>,
			<0 b2[0..6]>,
			<0 b3[0..6]>,
			<0 b4[0..6]>,
			<0 b5[0..6]>,
			<0 b6[0..6]>,
			<0 b7[0..6]>,
			<0 b1[7] b2[7] b3[7] b4[7] b5[7] b6[7] b7[7]>
	Example, when all checkboxes are checked:
	    byte 0 : 	 01111111
	    byte 1 : 	 01111111
	    byte 2 : 	 01111111
	    byte 3 : 	 01111111
	    byte 4 : 	 01111111
	    byte 5 : 	 01111111
	    byte 6 : 	 01111111
	    carry:   	 01111111
	    byte 7 : 	 01111111
	    byte 8 : 	 01111111
	    byte 9 : 	 01111111
	    byte 10 : 	 01111111
	    byte 11 : 	 01111111
	    byte 12 : 	 01111111
	    byte 13 : 	 01111111
	    carry:   	 01111111
	    byte 14 : 	 01111111
	    byte 15 : 	 01111111
	    byte 16 : 	 01111111
	    last car: 	 01110000

        '''
        '''
        carry = 0x00
        n = 0
        for channel in range(17):   # 16 channels + RT
            enabled_outputs = 0
            for output in range(8):
                if self.checkbox_states[output][channel]:
                    enabled_outputs |= (1 << output)

            truncated_byte = enabled_outputs & 0x7F           # First 7 bits                           mask is 0x7F (0111 1111)
            print("    byte", channel,": \t", format(truncated_byte, 'b').zfill(8))
            #sysex_message += [truncated_byte & 0x7F]
            sysex_message += [truncated_byte]

            #carry |= ((enabled_outputs & 0x80) >> 7) << n         # 8th bit is kept aside (1st channel is LSB)         mask is 0x80 (1000 0000)
            #carry |= (enabled_outputs & 0x80) >> 7 - n            # 8th bit is kept aside (identical as above)         mask is 0x80 (1000 0000)
            carry |= (enabled_outputs & 0x80) >> n + 1             # 8th bit is kept aside (reverse order: 1st is MSB)  mask is 0x80 (1000 0000)
            n = n + 1
            if n == 7:                                          # if 7 
                print("    carry:   \t", format(carry, 'b').zfill(8))
                sysex_message += [carry]
                carry = 0x00
                n = 0

        print("    last car: \t", format(carry, 'b').zfill(8))
        sysex_message += [carry]
        '''
        #enabled_outputs = []
        # Initialize array: × 17 channels
        enabled_outputs = [False for _ in range(17)]

        
        for channel in range(17):   # 16 channels + RT
            enabled_outputs[channel] = 0
            for output in range(8):
                if self.checkbox_states[output][channel]:
                    enabled_outputs[channel] |= (1 << output)

        print("Enabled outputs:")
        print(" ".join(f"{byte:02X}" for byte in enabled_outputs))

        packed_message = [0] * 20
        convert_to_7bit_message(enabled_outputs, packed_message)
        print("Packed message:")
        print(packed_message)
        sysex_message += packed_message
        sysex_message.append(SYSEX_END)  # close the message

        print("sysex message:")
        #print(sysex_message)
        print(" ".join(f"{byte:02X}" for byte in sysex_message))

        try:
            with open_output(selected_device) as outport:
                outport.send(Message('sysex', data=sysex_message[1:-1]))      # Exclude F0 and F7 as mido handles these internally for 'sysex' messages
                print("SysEx message sent!")
                #QMessageBox.information(self, "Success", f"Sent note to channels: {enabled_channels}")
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to send config through MIDI: {e}")

    '''
        def convert_to_7bit_message(bytes, packed_message):
            carry = 0x00
            carry_idx = 0
            packed_idx = 0
            carry_cnt = 0
            packed_data = 0x00
      
            for byte in bytes:
                if carry_cnt == 0:                             # first loop, and every 7 loops,
                    carry_idx = packed_idx                     # save carry position
                    packed_idx = packed_idx + 1                # next packed_data will be stored after carry

                # Store lower 7 bits
                packed_data = byte & 0x7F                      # First 7 bits, mask is 0x7F (0111 1111)

                # Collect MSBs in carry
                carry |= (byte & 0x80) >> carry_cnt + 1        # 8th bit is kept aside (reverse order: 1st is MSB)  mask is 0x80 (1000 0000)
                packed_message[carry_idx] = carry              # save carry at saved position

                carry_cnt = carry_cnt + 1
                if carry_cnt == 7:                             # 0 to 6: ok, 7: reset counter
                    #packed_message[carry_idx] = carry         # save carry at saved position
                    carry_cnt = 0                              # reset carry counter
                
                packed_message[packed_idx] = packed_data       # store packed data in message
                packed_idx = packed_idx + 1
    '''



    def closeEvent(self, event):
        # Save MIDI device dropdown combo settings before closing the application
        print("Saving settings...")
        self.settings.setValue("midi_output_device", self.midi_output_device_dropdown.currentText())
        self.settings.setValue("midi_input_device", self.midi_input_device_dropdown.currentText())
        super().closeEvent(event)

def convert_to_7bit_message(byte_message, packed_message):
    carry = 0x00
    carry_idx = 0
    packed_idx = 1
    carry_cnt = 0
    #packed_data = 0x00

    for byte in byte_message:
        '''
        print("\n  byte:   \t\t " + f'{byte:08b}' + ' (' + f'{byte:02X}' + ')')

        print("    carry_cnt:   \t", carry_cnt)
        print("    carry_idx:   \t", carry_idx)
        print("    packed_idx:  \t", packed_idx)
        '''
        # keep lower 7 bits
        #packed_data = byte & 0x7F                      # First 7 bits, mask is 0x7F (0111 1111)
        #print("    packed_data: \t " + f'{packed_data:08b}' + ' (' + f'{packed_data:02X}' + ')')

        #packed_message[packed_idx] = packed_data       # store packed data in message
        #packed_message += [byte & 0x7F]
        #packed_message += []                           # make some room
        packed_message[packed_idx] = byte & 0x7F        # Store first 7 bits, mask is 0x7F (0111 1111)
        packed_idx = packed_idx + 1

        # Collect MSBs in carry
        carry |= (byte & 0x80) >> carry_cnt + 1        # 8th bit is kept aside (reverse order: 1st is MSB)  mask is 0x80 (1000 0000)
        #print("    carry: \t\t " + f'{carry:08b}' + ' (' + f'{carry:02X}' + ')')
        carry_cnt = carry_cnt + 1

        if carry_cnt == 7:                             # if 7th byte
            carry_cnt = 0                              # reset carry counter
            #print ("  reset carry_cnt")

            packed_message[carry_idx] = carry          # save previous carry at previously saved position

            carry = 0x00                               # new carry
            
            carry_idx = packed_idx                     # save new carry position
            packed_idx = packed_idx + 1                # next packed_data will be stored after carry
        
        
    packed_message[carry_idx] = carry              # save last carry at saved position


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MidiApp()
    window.show()
    sys.exit(app.exec_())
