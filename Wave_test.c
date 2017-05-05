// 	uint16_t test_sample = fraction_of_FFF(45, 45);
// 
// 	active_voices[0].v_enable = true;
// 	active_voices[0].v_type = TRI;
// 	active_voices[0].v_counter = 0;
// 	active_voices[0].v_period = 50;
// 
// 	j=0;
// 
// 	//SQUARE TEST
// 	while(1)
// 	{
// 		if(active_voices[j].v_counter <= (active_voices[j].v_period)/2)
// 		{
// 			sample_buffer = (uint16_t) (0xFFF >> 2);
// 		}
// 		else sample_buffer = 0;
// 
// 		if(active_voices[0].v_counter < active_voices[0].v_period)
// 		{
// 			active_voices[0].v_counter++;
// 		}
// 		else active_voices[0].v_counter = 0;
// 	
// 		write_to_MCP4821(sample_buffer*2);
// 	}
// 
// 
// 	//TRIANGLE TEST
// 	while(1)
// 	{
// 
// 		if(active_voices[j].v_counter <= ((active_voices[j].v_period) >> 1))
// 		{
// 			sample_buffer = (uint16_t) (fraction_of_FFF((active_voices[j].v_counter << 1), active_voices[j].v_period) >> 2);
// 		}
// 		else if(active_voices[j].v_counter > ((active_voices[j].v_period) >> 1))
// 		{
// 			sample_buffer = (uint16_t) (fraction_of_FFF(((active_voices[j].v_period - active_voices[j].v_counter) << 1), active_voices[j].v_period) >> 2);
// 		}
// 
// 		if(active_voices[0].v_counter < active_voices[0].v_period)
// 		{
// 			active_voices[0].v_counter++;
// 		}
// 		else active_voices[0].v_counter = 0;
// 
// 		write_to_MCP4821(sample_buffer*3);
// 	}
// 
// 
// 	//SAW TEST
// 	while(1)
// 	{
// 
// 		sample_buffer = (uint16_t) ((0xFFF * (active_voices[0].v_counter/active_voices[0].v_period)) >> 2);
// 
// 
// 		if(active_voices[0].v_counter < active_voices[0].v_period)
// 		{
// 			active_voices[0].v_counter++;
// 		}
// 		else active_voices[0].v_counter = 0;
// 	
// 		write_to_MCP4821(sample_buffer*2);
// 	}

// 	//global variable inits
// 	full_queue_flag = false;
// 
// 	for (n=0; n<4; n++)
// 	{
// 		active_voices[n].v_enable = false;
// 		active_voices[n].v_counter = 0;
// 	}
// 
// 	active_voices[0].v_enable = true;
// 	active_voices[0].v_type = SQUARE;
// 	active_voices[0].v_counter = 0;
// 	active_voices[0].v_period = 45;
// 