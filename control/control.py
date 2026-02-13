#!/usr/bin/env python3
"""
MIDI 1-to-8 Configuration Interface

Graphical interface to configure the MIDI 1-to-8 eurorack module.
Allows defining MIDI channel routing to 8 outputs via SysEx.
Supports 8 independent presets (configurations) selectable via Program Change on channel 16.


"""

import sys
import json
import time
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QVBoxLayout, QHBoxLayout, QGridLayout,
    QWidget, QLabel, QComboBox, QCheckBox, QPushButton, QMessageBox,
    QStatusBar, QGroupBox
)
from PyQt5.QtGui import QPixmap
from PyQt5.QtCore import QSettings, Qt
from mido import get_output_names, open_output, get_input_names, open_input, Message

# ============================================================================
# SysEx Protocol Constants
# ============================================================================
SYSEX_START   = 0xF0
SYSEX_END     = 0xF7
MANUFACTURER  = 0x7D  # Non-official ID for experiments and DIY
MODEL         = 0x18  # MIDI 1-8
DEVICE        = 0x01  # First device
MANAGER       = 0x00  # Manager ID for responses

COMMAND_PING_DEVICE        = 0x01
COMMAND_READ_PRESET        = 0x02
COMMAND_WRITE_PRESET       = 0x03
COMMAND_CHANGE_DEVICE_ID   = 0x04
COMMAND_SWITCH_PRESET      = 0x05

NBR_PRESETS = 8
MATRIX_SIZE = 17
PRESET_CHANGE_CHANNEL = 16  # Channel 16 for preset switching

RESPONSE_TIMEOUT_S = 2.0  # 2 seconds timeout for device response

# ============================================================================
# 7-bit Encoding/Decoding Functions
# ============================================================================
def convert_to_7bit_message(byte_message):
    """Encode 8-bit bytes into 7-bit format for SysEx"""
    packed_message = [0] * ((len(byte_message) * 8 + 6) // 7)
    carry = 0x00
    carry_idx = 0
    packed_idx = 1
    carry_cnt = 0
    
    for byte in byte_message:
        packed_message[packed_idx] = byte & 0x7F
        packed_idx += 1
        carry |= ((byte & 0x80) >> 7) << carry_cnt
        carry_cnt += 1
        
        if carry_cnt == 7:
            carry_cnt = 0
            packed_message[carry_idx] = carry
            carry = 0x00
            carry_idx = packed_idx
            packed_idx += 1
    
    packed_message[carry_idx] = carry
    return packed_message

def convert_from_7bit_message(packed_message):
    """Decode 7-bit SysEx data back to 8-bit"""
    retrieved_bytes = []
    carry_cnt = 0
    carry = 0x00
    
    for byte in packed_message:
        if carry_cnt == 0:
            carry = byte
        else:
            reconstructed = byte | (((carry >> (carry_cnt - 1)) & 0x01) << 7)
            retrieved_bytes.append(reconstructed)
        
        carry_cnt += 1
        if carry_cnt == 8:
            carry_cnt = 0
    
    return retrieved_bytes

# ============================================================================
# Main Application Window
# ============================================================================
class MidiApp(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("MIDI 1-8 Setup")
        self.resize(700, 500)
        
        # Settings for persistence
        self.settings = QSettings("David Haillant", "MIDI 1-8")
        print(f"Settings stored in: {self.settings.fileName()}")
        
        # 8 preset matrices (3D array: [preset][output][channel])
        self.preset_matrices = [[[False for _ in range(17)] for _ in range(8)] for _ in range(NBR_PRESETS)]
        
        # Current preset number
        self.current_preset = 0
        
        # MIDI ports (kept open)
        self.midi_output_port = None
        self.midi_input_port = None
        
        # Build UI
        self._build_ui()
        
        # Load saved configuration (this will trigger port opening via callbacks)
        self._load_settings()
        
        # Display current preset matrix
        self._load_preset_into_ui(self.current_preset)
        
        # Status bar
        self.statusBar = QStatusBar()
        self.setStatusBar(self.statusBar)
        self.statusBar.showMessage("Ready")
    
    def _build_ui(self):
        """Build user interface"""
        # Main horizontal layout
        main_hbox = QHBoxLayout()
        
        # Front panel image (optional)
        try:
            side_picture = QLabel()
            side_picture.setPixmap(QPixmap('frontpanel_62x400.png'))
            main_hbox.addWidget(side_picture)
        except:
            pass  # Image is optional
        
        # Vertical layout for controls
        controls_vbox = QVBoxLayout()
        main_hbox.addLayout(controls_vbox)
        
        # === MIDI Devices and Ping Button ===
        devices_group = QGroupBox("MIDI Connection")
        devices_layout = QHBoxLayout()
        
        # Left column: dropdowns
        dropdowns_vbox = QVBoxLayout()
        
        # MIDI Output Device
        output_layout = QHBoxLayout()
        output_layout.addWidget(QLabel("Output:"))
        self.midi_output_dropdown = QComboBox()
        self.midi_output_dropdown_items = get_output_names()
        self.midi_output_dropdown.addItems(self.midi_output_dropdown_items)
        self.midi_output_dropdown.setMinimumWidth(200)
        self.midi_output_dropdown.setToolTip(
            "Connect to MIDI Input on module"
        )
        self.midi_output_dropdown.currentIndexChanged.connect(self._on_output_device_changed)
        output_layout.addWidget(self.midi_output_dropdown)
        dropdowns_vbox.addLayout(output_layout)
        
        # MIDI Input Device with tooltip
        input_layout = QHBoxLayout()
        input_layout.addWidget(QLabel("Input:"))
        self.midi_input_dropdown = QComboBox()
        self.midi_input_dropdown_items = get_input_names()
        self.midi_input_dropdown.addItems(self.midi_input_dropdown_items)
        self.midi_input_dropdown.setMinimumWidth(200)
        self.midi_input_dropdown.setToolTip(
            "Connect to Output 8 on module"
        )
        self.midi_input_dropdown.currentIndexChanged.connect(self._on_input_device_changed)
        input_layout.addWidget(self.midi_input_dropdown)
        dropdowns_vbox.addLayout(input_layout)
        
        devices_layout.addLayout(dropdowns_vbox)
        devices_layout.addStretch(1)
        
        # Right side: Ping button
        self.ping_button = QPushButton("Ping\nDevice")
        self.ping_button.setMinimumHeight(60)
        self.ping_button.setMinimumWidth(80)
        self.ping_button.setToolTip("Test device presence")
        self.ping_button.clicked.connect(self._ping_device)
        devices_layout.addWidget(self.ping_button)
        
        devices_group.setLayout(devices_layout)
        controls_vbox.addWidget(devices_group)
        
        controls_vbox.addSpacing(10)
        
        # === Preset Selection ===
        preset_group = QGroupBox("Preset Selection")
        preset_layout = QHBoxLayout()
        
        # Previous button
        self.prev_button = QPushButton("◀ Prev")
        self.prev_button.setMinimumHeight(30)
        self.prev_button.setMinimumWidth(80)
        self.prev_button.clicked.connect(self._prev_preset)
        preset_layout.addWidget(self.prev_button)
        
        # Current preset display
        self.preset_label = QLabel(f"<b>Preset {self.current_preset + 1}</b>")
        self.preset_label.setAlignment(Qt.AlignCenter)
        self.preset_label.setMinimumWidth(80)
        #self.preset_label.setStyleSheet("font-size: 14pt;")
        preset_layout.addWidget(self.preset_label)
        
        # Next button
        self.next_button = QPushButton("Next ▶")
        self.next_button.setMinimumHeight(30)
        self.next_button.setMinimumWidth(80)
        self.next_button.clicked.connect(self._next_preset)
        preset_layout.addWidget(self.next_button)
        
        preset_layout.addSpacing(20)
        
        # Switch preset button
        switch_button = QPushButton("Switch Preset on Device")
        switch_button.setMinimumHeight(30)
        switch_button.setToolTip(
            f"Send Program Change on channel {PRESET_CHANGE_CHANNEL} to switch active preset on device"
        )
        switch_button.clicked.connect(self._switch_preset_on_device)
        preset_layout.addWidget(switch_button)
        
        preset_layout.addStretch(1)
        
        
        preset_group.setLayout(preset_layout)
        controls_vbox.addWidget(preset_group)
        
        controls_vbox.addSpacing(10)
        
        # === Routing Matrix ===
        self.matrix_grid = QGridLayout()
        
        # Column headers (MIDI channels 1-16 + RT)
        self.matrix_grid.addWidget(QLabel("Channels:"), 0, 0)
        for i in range(16):
            header = QLabel(f"<b>{i + 1}</b>")
            #header.setAlignment(Qt.AlignCenter)
            self.matrix_grid.addWidget(header, 0, i + 1)
        rt_header = QLabel("<b>RT</b>")
        rt_header.setAlignment(Qt.AlignCenter)
        self.matrix_grid.addWidget(rt_header, 0, 17)
        
        # Matrix rows (8 outputs)
        self.checkboxes = []
        for row in range(8):
            row_checkboxes = []
            
            # Output label
            output_label = QLabel(f"<b>Output {row + 1}</b>")
            self.matrix_grid.addWidget(output_label, row + 1, 0)
            
            # 17 checkboxes (16 channels + RT)
            for col in range(17):
                checkbox = QCheckBox()
                checkbox.setStyleSheet("QCheckBox::indicator { width: 15px; height: 15px; }")
                self.matrix_grid.addWidget(checkbox, row + 1, col + 1)
                checkbox.stateChanged.connect(
                    lambda state, r=row, c=col: self._update_checkbox_state(r, c, state)
                )
                row_checkboxes.append(checkbox)
            
            self.checkboxes.append(row_checkboxes)
            
            # "All" and "None" buttons
            check_all_button = QPushButton("All")
            check_all_button.clicked.connect(lambda _, r=row: self._toggle_row(r, True))
            self.matrix_grid.addWidget(check_all_button, row + 1, 18)
            
            check_none_button = QPushButton("None")
            check_none_button.clicked.connect(lambda _, r=row: self._toggle_row(r, False))
            self.matrix_grid.addWidget(check_none_button, row + 1, 19)
        
        controls_vbox.addLayout(self.matrix_grid)
        controls_vbox.addStretch(1)
        
        controls_vbox.addSpacing(10)
        
        # === Communication Buttons ===
        comm_layout = QHBoxLayout()
        
        # First row: Read buttons
        #read_layout = QHBoxLayout()
        
        self.read_current_button = QPushButton("Read Current Preset")
        self.read_current_button.setToolTip("Read the selected preset from device")
        self.read_current_button.clicked.connect(self._read_current_preset)
        #read_layout.addWidget(self.read_current_button)
        comm_layout.addWidget(self.read_current_button)
        
        self.read_all_button = QPushButton("Read All Presets")
        self.read_all_button.setToolTip("Read all 8 presets from device")
        self.read_all_button.clicked.connect(self._read_all_presets)
        #read_layout.addWidget(self.read_all_button)
        comm_layout.addWidget(self.read_all_button)
        
        #comm_layout.addLayout(read_layout)
        
        # Second row: Write buttons
        #write_layout = QHBoxLayout()
        
        self.write_current_button = QPushButton("Write Current Preset")
        self.write_current_button.setToolTip("Write the selected preset to device")
        self.write_current_button.clicked.connect(self._write_current_preset)
        #write_layout.addWidget(self.write_current_button)
        comm_layout.addWidget(self.write_current_button)
        
        self.write_all_button = QPushButton("Write All Presets")
        self.write_all_button.setToolTip("Write all 8 presets to device")
        self.write_all_button.clicked.connect(self._write_all_presets)
        #write_layout.addWidget(self.write_all_button)
        comm_layout.addWidget(self.write_all_button)
        
        #comm_layout.addLayout(write_layout)
        
        controls_vbox.addLayout(comm_layout)
        
        # Central widget
        central_widget = QWidget()
        central_widget.setLayout(main_hbox)
        self.setCentralWidget(central_widget)
    
    # ========================================================================
    # MIDI Port Management
    # ========================================================================
    
    def _on_output_device_changed(self, index):
        """When output device changes, reopen port"""
        if self.midi_output_port:
            self.midi_output_port.close()
            self.midi_output_port = None
        
        if index >= 0:
            port_name = self.midi_output_dropdown.currentText()
            if port_name:
                try:
                    self.midi_output_port = open_output(port_name)
                    print(f"MIDI Output opened: {port_name}")
                except Exception as e:
                    print(f"Failed to open MIDI output: {e}")
    
    def _on_input_device_changed(self, index):
        """When input device changes, reopen port"""
        if self.midi_input_port:
            self.midi_input_port.close()
            self.midi_input_port = None
        
        if index >= 0:
            port_name = self.midi_input_dropdown.currentText()
            if port_name:
                try:
                    self.midi_input_port = open_input(port_name)
                    print(f"MIDI Input opened: {port_name}")
                except Exception as e:
                    print(f"Failed to open MIDI input: {e}")
    
    # ========================================================================
    # Preset Management
    # ========================================================================
    
    def _prev_preset(self):
        """Go to previous preset"""
        new_preset = (self.current_preset - 1) % NBR_PRESETS
        self._change_preset(new_preset)
    
    def _next_preset(self):
        """Go to next preset"""
        new_preset = (self.current_preset + 1) % NBR_PRESETS
        self._change_preset(new_preset)
    
    def _change_preset(self, new_preset):
        """Change to specified preset"""
        # Save current UI state to old preset
        self._save_ui_to_preset(self.current_preset)
        
        # Update current preset
        self.current_preset = new_preset
        
        # Update label
        self.preset_label.setText(f"<b>Preset {self.current_preset + 1}</b>")
        
        # Load new preset to UI
        self._load_preset_into_ui(self.current_preset)
        
        self.statusBar.showMessage(f"Showing preset {new_preset + 1}", 2000)
    
    def _save_ui_to_preset(self, preset_num):
        """Save current UI checkbox states to specified preset"""
        for row in range(8):
            for col in range(17):
                self.preset_matrices[preset_num][row][col] = self.checkboxes[row][col].isChecked()
    
    def _load_preset_into_ui(self, preset_num):
        """Load specified preset into UI checkboxes"""
        for row in range(8):
            for col in range(17):
                checked = self.preset_matrices[preset_num][row][col]
                self.checkboxes[row][col].blockSignals(True)
                self.checkboxes[row][col].setChecked(checked)
                self.checkboxes[row][col].blockSignals(False)
    
    
    # ========================================================================
    # Matrix Management
    # ========================================================================
    
    def _update_checkbox_state(self, row, col, state):
        """Update internal state when a checkbox changes"""
        self.preset_matrices[self.current_preset][row][col] = (state == Qt.Checked)
    
    def _toggle_row(self, row, check_state):
        """Enable/disable all checkboxes in a row"""
        for col in range(17):
            self.checkboxes[row][col].setChecked(check_state)
            self.preset_matrices[self.current_preset][row][col] = check_state
    
    def _get_preset_matrix_data(self, preset_num):
        """Get matrix data for a specific preset"""
        matrix_data = []
        for channel in range(17):
            value = 0
            for output in range(8):
                if self.preset_matrices[preset_num][output][channel]:
                    value |= (1 << output)
            matrix_data.append(value)
        return matrix_data
    
    def _set_preset_matrix_data(self, preset_num, matrix_data):
        """Set matrix data for a specific preset"""
        if len(matrix_data) != 17:
            print(f"Error: invalid matrix (length {len(matrix_data)})")
            return
        
        for channel in range(17):
            for output in range(8):
                checked = bool(matrix_data[channel] & (1 << output))
                self.preset_matrices[preset_num][output][channel] = checked
        
        # If it's the current preset, update UI
        if preset_num == self.current_preset:
            self._load_preset_into_ui(preset_num)
    
    
    
    
    def _send_sysex(self, command, payload=None):
        """Send a SysEx message to the module"""
        if not self.midi_output_port:
            self.statusBar.showMessage("No MIDI Output device connected", 5000)
            return False
        
        sysex_message = [MANUFACTURER, MODEL, DEVICE, command]
        if payload:
            sysex_message.extend(payload)
        
        print(f"Sending SysEx:  F0 {' '.join(f'{b:02X}' for b in sysex_message)} F7")
        
        try:
            self.midi_output_port.send(Message('sysex', data=sysex_message))
            return True
        except Exception as e:
            self.statusBar.showMessage(f"Send failed: {e}", 5000)
            return False
    
    def _wait_for_sysex_response(self, timeout=RESPONSE_TIMEOUT_S):
        """Wait for SysEx response synchronously"""
        if not self.midi_input_port:
            return None
        
        start_time = time.time()
        
        while time.time() - start_time < timeout:
            # Keep UI responsive
            QApplication.processEvents()
            
            # Check for incoming message
            msg = self.midi_input_port.poll()
            if msg and msg.type == 'sysex':
                # Reconstruct complete SysEx
                sysex_data = [SYSEX_START] + list(msg.data) + [SYSEX_END]
                
                print(f"Received SysEx: {' '.join(f'{b:02X}' for b in sysex_data)}")
                
                # Check if it's a response FROM the module (device_id = MANAGER = 0x00)
                # Reject messages TO the module (device_id = 0x01) that were forwarded back
                if (len(sysex_data) >= 6 and 
                    sysex_data[1] == MANUFACTURER and 
                    sysex_data[2] == MODEL and
                    sysex_data[3] == MANAGER):  # Must be from MANAGER (0x00), not DEVICE (0x01)
                    print(f"  Valid response from module (device_id={sysex_data[3]:02X})")
                    return sysex_data
                else:
                    print(f"  Rejected (manuf={sysex_data[1]:02X if len(sysex_data)>1 else '?'}, model={sysex_data[2]:02X if len(sysex_data)>2 else '?'}, device={sysex_data[3]:02X if len(sysex_data)>3 else '?'})")
            
            time.sleep(0.01)  # 10ms
        
        return None  # Timeout
    
    def _disable_comm_buttons(self, disabled):
        """Disable/enable communication buttons"""
        self.ping_button.setEnabled(not disabled)
        self.read_current_button.setEnabled(not disabled)
        self.read_all_button.setEnabled(not disabled)
        self.write_current_button.setEnabled(not disabled)
        self.write_all_button.setEnabled(not disabled)
    
    def _ping_device(self):
        """Send PING to module and wait for response"""
        self._disable_comm_buttons(True)
        QApplication.setOverrideCursor(Qt.WaitCursor)
        
        try:
            if not self._send_sysex(COMMAND_PING_DEVICE):
                return
            
            self.statusBar.showMessage("Waiting for response...", 2000)
            response = self._wait_for_sysex_response()
            
            if response:
                # Parse response (same as READ_PRESET)
                if len(response) >= 27:
                    preset_num = response[5]
                    packed_data = response[6:-1]
                    if len(packed_data) == 20:
                        matrix_data = convert_from_7bit_message(packed_data)
                        if len(matrix_data) >= 17:
                            #self._set_preset_matrix_data(preset_num, matrix_data[:17])
                            self.statusBar.showMessage(f"Device found!", 5000)
                            return
                
                self.statusBar.showMessage("Invalid response from device", 5000)
            else:
                self.statusBar.showMessage("No response - Check connection to OUTPUT 8", 5000)
        
        finally:
            QApplication.restoreOverrideCursor()
            self._disable_comm_buttons(False)
    
    def _read_preset_from_device(self, preset_num):
        """
        Request a single preset from the module
        
        Args:
            preset_num: Preset number (0-7) to read
        
        Returns:
            bool: True if successful, False otherwise
        """
        payload = [preset_num]
        if not self._send_sysex(COMMAND_READ_PRESET, payload):
            return False
        
        response = self._wait_for_sysex_response()
        
        if response and len(response) >= 27:
            received_preset_num = response[5]
            packed_data = response[6:-1]
            if len(packed_data) == 20:
                matrix_data = convert_from_7bit_message(packed_data)
                if len(matrix_data) >= 17:
                    self._set_preset_matrix_data(received_preset_num, matrix_data[:17])
                    return True
        
        return False
    
    def _read_current_preset(self):
        """Request and wait for current preset"""
        self._disable_comm_buttons(True)
        QApplication.setOverrideCursor(Qt.WaitCursor)
        
        try:
            self.statusBar.showMessage(f"Reading preset {self.current_preset + 1}...", 2000)
            
            if self._read_preset_from_device(self.current_preset):
                self.statusBar.showMessage(f"Preset {self.current_preset + 1} loaded from device", 5000)
            else:
                self.statusBar.showMessage("No response from module", 5000)
        
        finally:
            QApplication.restoreOverrideCursor()
            self._disable_comm_buttons(False)
    
    def _read_all_presets(self):
        """Request and wait for all 8 presets"""
        self._disable_comm_buttons(True)
        QApplication.setOverrideCursor(Qt.WaitCursor)
        
        try:
            received_count = 0
            
            for preset in range(NBR_PRESETS):
                self.statusBar.showMessage(f"Reading presets... {preset + 1}/8", 1000)
                QApplication.processEvents()
                
                # Send individual READ_PRESET command for each preset
                if self._read_preset_from_device(preset):
                    received_count += 1
                else:
                    break  # Stop on first failure
            
            if received_count == NBR_PRESETS:
                self.statusBar.showMessage(f"All {NBR_PRESETS} presets loaded from device", 5000)
            else:
                self.statusBar.showMessage(f"Only {received_count}/{NBR_PRESETS} presets received", 5000)
        
        finally:
            QApplication.restoreOverrideCursor()
            self._disable_comm_buttons(False)
    
    def _write_preset_to_device(self, preset_num):
        """
        Send a single preset to the module
        
        Args:
            preset_num: Preset number (0-7) to send
        
        Returns:
            bool: True if successful, False otherwise
        """
        matrix_data = self._get_preset_matrix_data(preset_num)
        packed_data = convert_to_7bit_message(matrix_data)
        payload = [preset_num] + packed_data
        
        return self._send_sysex(COMMAND_WRITE_PRESET, payload)
    
    def _write_current_preset(self):
        """Send current preset to module"""
        self._save_ui_to_preset(self.current_preset)
        
        if self._write_preset_to_device(self.current_preset):
            self.statusBar.showMessage(f"Preset {self.current_preset + 1} written to device", 5000)
    
    def _write_all_presets(self):
        """Send all 8 presets to module"""
        self._save_ui_to_preset(self.current_preset)
        
        QApplication.setOverrideCursor(Qt.WaitCursor)
        try:
            for preset in range(NBR_PRESETS):
                if not self._write_preset_to_device(preset):
                    self.statusBar.showMessage(f"Failed to send preset {preset}", 5000)
                    return
                
                self.statusBar.showMessage(f"Writing presets... {preset + 1}/8", 500)
                QApplication.processEvents()
                
                # Longer delay to let module process each preset
                # Module needs time to decode, write to EEPROM, etc.
                time.sleep(0.25)  # 250ms delay between presets
            
            self.statusBar.showMessage("All 8 presets written to device", 5000)
        
        finally:
            QApplication.restoreOverrideCursor()
    
    def _switch_preset_on_device(self):
        """Send Program Change on channel 16 to switch preset on device"""
        if not self.midi_output_port:
            self.statusBar.showMessage("⚠ No MIDI Output device connected", 5000)
            return
        
        print(f"Sending Program Change {self.current_preset + 1} on channel {PRESET_CHANGE_CHANNEL}")

        try:
            self.midi_output_port.send(
                Message('program_change', program=self.current_preset, channel=PRESET_CHANGE_CHANNEL - 1)
            )
            self.statusBar.showMessage(
                f"Preset {self.current_preset + 1} activated - LED will blink {self.current_preset + 1} time(s)", 5000)
        except Exception as e:
            self.statusBar.showMessage(f"✗ Send failed: {e}", 5000)
    # ========================================================================
    
    def _save_settings(self):
        """Save configuration to QSettings"""
        self._save_ui_to_preset(self.current_preset)
        
        self.settings.setValue("midi_output_device", self.midi_output_dropdown.currentText())
        self.settings.setValue("midi_input_device", self.midi_input_dropdown.currentText())
        self.settings.setValue("current_preset", self.current_preset)
        
        matrices_json = json.dumps(self.preset_matrices)
        self.settings.setValue("preset_matrices", matrices_json)
        
        print("Configuration saved")
    
    def _load_settings(self):
        """Load configuration from QSettings"""
        saved_output = self.settings.value("midi_output_device")
        if saved_output and saved_output in self.midi_output_dropdown_items:
            index = self.midi_output_dropdown.findText(saved_output)
            self.midi_output_dropdown.setCurrentIndex(index)
        
        saved_input = self.settings.value("midi_input_device")
        if saved_input and saved_input in self.midi_input_dropdown_items:
            index = self.midi_input_dropdown.findText(saved_input)
            self.midi_input_dropdown.setCurrentIndex(index)
        
        saved_preset = self.settings.value("current_preset", 0, type=int)
        if 0 <= saved_preset < NBR_PRESETS:
            self.current_preset = saved_preset
            self.preset_label.setText(f"<b>Preset {self.current_preset + 1}</b>")
        
        matrices_json = self.settings.value("preset_matrices")
        if matrices_json:
            try:
                loaded_matrices = json.loads(matrices_json)
                if (len(loaded_matrices) == NBR_PRESETS and 
                    all(len(preset) == 8 for preset in loaded_matrices) and
                    all(len(row) == 17 for preset in loaded_matrices for row in preset)):
                    self.preset_matrices = loaded_matrices
                    print("All presets loaded from configuration")
            except:
                print("Error loading presets")
    
    def closeEvent(self, event):
        """Save configuration and close ports before closing"""
        # Close MIDI ports
        if self.midi_output_port:
            self.midi_output_port.close()
        if self.midi_input_port:
            self.midi_input_port.close()
        
        # Save
        self._save_settings()
        
        super().closeEvent(event)

# ============================================================================
# Entry Point
# ============================================================================
if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MidiApp()
    window.show()
    sys.exit(app.exec_())
