# OpenYap
### Remove Silence From Video
### By: Derek Spaulding

This project is still in development. However, there is some functionality currently as of 2025-10-5. The main.cpp file can be customized from this statement:

std::string inputName = "input.mp4";

std::string outputName = "output.mp4";

float db_volume_threshold = -18.0f;

float dead_space_buffer = 0.5f;

The inputName is the input file name, and outputName is obvious. db_volume_threshold is the decibel level limit of "silence", meaning all volume below this threshold will be treated as silence. Then, with the dead_space_buffer, it is in seconds. It appends dead space to each segment of video to pad the speaker with a bit of dead space to avoid jarring jumps.

### Performance
As of 2025-10-5, the performance of this project on a 2 1/2 hour video took about 13 seconds to create an output. The output turned into a 57 minute video.

### Accuracy
This current iteration processes 32 samples per audio packet. However, for the most part I can sample 1 frame. It will drop accuracy to a 23ms window. However, the big hurdle is the dead space buffer. There can be UP TO (< dead_space_buffer) an addition buffer size of variance in the deadspace. However, the "audible" video segments I do believe maintain their structure regardless. (Like no sharp cut-ins). I would suggest using the 0.5f and make incremental changes if needed.

### Technical Breakdown
I handled the AV packets directly in this application. I only decode the audio packet to sample the db levels. The data structure is like this:
GOP Queue - There are video and audio packets that fill the queue. When a video keyframe is found, it checks to see if the running duration of the queue doesn't exceed the dead space buffer. When it does, it puts the queue into another queue. This is the write out buffer.
Write Out Buffer - This is a queue that holds 2 GOP queues. A state machine checks the queue based on the state of both GOP queues. The state that is being checked if the GOP queues are marked as "audible" (1).
State Machine - I came up with this to solve for how to write out to the output. This was before I learned about circular buffers, but I do enjoy this route as it's less modulo instructions. This state machine is broken down into 4 different states (since there are 2 queues in the buffer):

00 - Both are not "audible", SM pops one GOP queue

01 - One not "audible", then one "audible". SM writes one GOP to output

10 - One "audible", then one not. SM writes both GOP queues to output

11 - Both "audible", SM writes one GOP queue to output

Took me a while and a few long walks to come up with the algorithm. But, it was very rewarding when it got done!

### Current Issues
I am attempting to solve for PTS/DTS mishandling from media, such as streamed content from Twitch. I was originally creating this as a means of editting VODs. However, if there are dropped video packets in the file, the output becomes desynced. I currently have it where the buffer gets flushed when a jump in PTS/DTS occurs between video packets. This is because Twitch leaves in the audio packets in the file, but the PTS of the subsequent packet makes a typical video player jump ahead in the file. It works to some extent, but the desync is still there. For the time being, this works pretty decently with recorded video.

I am also working on fixing time scale in the event of packet loss from streaming. I will more than likely force 60/30/24 frames per second to ensure the output is clean. This will probably be done with approximation to what the input file shows. (For example, my example input is 59.96 fps due to frame drops).
