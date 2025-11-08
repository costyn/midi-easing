Summary of Solution
The Problem:
During rapid controller movements, Modulaser echoes lag behind
Echo values differ from current controller position by >= threshold (3)
This triggers unwanted unlatch, causing WAITING messages
The Solution: We implemented echo detection with a time window:
Track Recent Send Window (midi-easing-proxy.cpp:58-60)
recent_send_min/max: Min/max values sent in last 500ms
last_send_time: Timestamp of last send
Update Window on Send (midi-easing-proxy.cpp:133-148)
Every sendMidiCC updates the send window
Window resets after 500ms of inactivity
Ignore Echoes During Window (midi-easing-proxy.cpp:193-221)
If Modulaser value is within recent send range ± threshold
AND within 500ms of last send
Then it's an echo → ignore it
This handles both:
Easing echoes (already handled by is_easing check)
Rapid small-movement echoes (new fix)
