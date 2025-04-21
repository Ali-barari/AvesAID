# Adding and Forwarding Custom MAVLink Commands in PX4

## Goal
Add a custom MAVLink command (`MAV_CMD_START_FLIGHT`) to PX4 and ensure it gets properly forwarded to other components.

## Steps to Add a Custom Command

1. **Define the Command in MAVLink Message Definitions**
   - Add the command to `/src/modules/mavlink/mavlink/message_definitions/v1.0/SKYRON.xml`:
   ```xml
   <message id="42424" name="MAV_CMD_START_FLIGHT">
       <description>Start flight command</description>
       <field type="uint8_t" name="target_system">System which should execute the command</field>
       <field type="uint8_t" name="target_component">Component which should execute the command</field>
       <field type="uint8_t" name="confirmation">0: First transmission of this command</field>
       <field type="float" name="param1">Parameter 1</field>
       <field type="float" name="param2">Parameter 2</field>
       <field type="float" name="param3">Parameter 3</field>
       <field type="float" name="param4">Parameter 4</field>
       <field type="float" name="param5">Parameter 5</field>
       <field type="float" name="param6">Parameter 6</field>
       <field type="float" name="param7">Parameter 7</field>
   </message>
   ```

2. **Generate MAVLink Headers**
   - After adding the command to the XML file, you need to regenerate the MAVLink headers
   - This will create the necessary definitions in the generated header files

3. **Modify evaluate_target_ok**
   - In `src/modules/mavlink/mavlink_receiver.cpp`, add the command to `evaluate_target_ok`:
   ```cpp
   bool MavlinkReceiver::evaluate_target_ok(int command, int target_system, int target_component)
   {
       // ... existing checks ...

       case MAV_CMD_START_FLIGHT:
           // Allow forwarding of START_FLIGHT command
           return true;

       // ... rest of the function ...
   }
   ```

4. **Handle the Command**
   - In `src/modules/mavlink/mavlink_receiver.cpp`, add a handler for the command:
   ```cpp
   else if (cmd_mavlink.command == MAV_CMD_START_FLIGHT) {
       _mavlink.send_statustext_info("AvesAID: start flight command received");
       result = vehicle_command_ack_s::VEHICLE_CMD_RESULT_ACCEPTED;
       send_ack = true;
       _mavlink.send_statustext_info("AvesAID: start flight command accepted");
   }
   ```

5. **Enable Command Forwarding**
   - Modified `component_was_seen` in `mavlink_receiver.cpp` to properly track components:
   ```cpp
   bool MavlinkReceiver::component_was_seen(int system_id, int component_id)
   {
       // ... existing checks ...

       // Add component to list if not found
       if (_component_states_count < sizeof(_component_states) / sizeof(_component_states[0])) {
           _component_states[_component_states_count].system_id = system_id;
           _component_states[_component_states_count].component_id = component_id;
           _component_states_count++;
           return true;
       }
       return false;
   }
   ```

6. **Add Debug Messages**
   - Added debug prints in `mavlink_main.cpp` to track command forwarding:
   ```cpp
   if (msg->msgid == MAVLINK_MSG_ID_COMMAND_LONG || msg->msgid == MAVLINK_MSG_ID_COMMAND_INT) {
		// Only process commands that aren't for us
		if (target_system_id != self->get_system_id() || target_component_id != self->get_component_id()) {
			char buf[200];
			snprintf(buf, sizeof(buf), "AvesAID: Command msg_id: %lu, target_sys: %d, target_comp: %d, self_sys: %d, self_comp: %d",
				msg->msgid, target_system_id, target_component_id, self->get_system_id(), self->get_component_id());
			self->send_statustext_info(buf);

			// Mark the target component as seen for all instances
			LockGuard lg{mavlink_module_mutex};
			for (Mavlink *inst : mavlink_module_instances) {
				if (inst) {
					inst->_receiver.component_was_seen(target_system_id, target_component_id);
					snprintf(buf, sizeof(buf), "AvesAID: Marked component %d as seen for instance %d",
						target_component_id, inst->get_instance_id());
					self->send_statustext_info(buf);
				}
			}
		}
	}

   ```

## Testing the Command

1. **Send the Command**
   ```python
   # Example using pymavlink
   msg = vehicle.message_factory.command_long_encode(
       target_system=1,
       target_component=191,  # Target component ID
       command=MAV_CMD_START_FLIGHT,
       confirmation=0,
       param1=0, param2=0, param3=0, param4=0, param5=0, param6=0, param7=0
   )
   vehicle.send_mavlink(msg)
   ```

2. **Check Debug Output**
   You should see messages like:
   ```
   AvesAID: start flight command received
   AvesAID: start flight command accepted
   ```

## Common Issues and Solutions

1. **Command Not Forwarded**
   - Check if target component is in the seen components list
   - Verify forwarding is enabled for the instance
   - Ensure command is not for our own component
   - Make sure command is added to `evaluate_target_ok`

2. **Component Not Seen**
   - The component must be added to the seen components list
   - Maximum of 16 components can be tracked
   - Use `component_was_seen` to add the component

3. **Debug Messages Not Showing**
   - Messages only show for commands that need forwarding
   - Check if command is for our own component
   - Verify debug prints are enabled

## Notes
- The system maintains a list of up to 16 seen components
- Commands are only forwarded if the target component has been seen
- Debug messages help track the forwarding process
- Heartbeats are not forwarded unless specifically enabled
- Custom commands must be added to `evaluate_target_ok` to be processed
