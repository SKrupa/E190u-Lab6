##Introduction
The purpose of this lab was to integrate the motion detection software we wrote in the last lab with a game of pong. This could be achieved in any way we wanted to.

##Design Methodology
There were 4 possible approaches for this lab. The first would be to have the C++ code press keyboard keys to control the Pong paddle. I decided against this approach because it would be awkward to try to map analog movement to digital keys. It would require some PWM of pressing up or down with the duty cycle controlling speed upwards or downwards.

A similar, but in my opinion more straight forward approach, would be to map the hand movement to the mouse. This would be fairly easy to implement in C++ as well as the python pong game. Ultimately I decided against it because it would be troublesome to troubleshoot if the mouse was hijacked by the program. This is still a very viable option, but I personally chose against it.

The third option was to write Pong in C++ and have a unified Pong and motion detection program. This would work really well, but would require a lot of additional work to get Pong up and running from scratch.

Finally, I chose to interface the C++ and Python code directly by sending two ints containing X and Y position from the C++ program to the Python program. At first I planned on calling the C++ program using subprocesses in Python, allowing the Python to read standard out. I was getting permissions problems, so I abandoned this appoach. Instead, I created a socket with the C++ program being the server and the Python connecting as a client. The C++ program then simply sent a packet containing the ints to which the Python listened.

Once I had X and Y data in Python, I simply mapped the Y position to the paddle's position. I used the X position to activate a powerup. Specifically, if the player scored 3 in a row, they recieved a powerup which would allow them to decrement the opponen's score by one. To make this flashy, I made the action to activate this powerup a punch. Thus if the player punches fast enough to the left while they have the powerup, the opponents score will decrease by one. If the user punches too slowly or has no such powerup, nothing happens. 

##Testing Methodology
The first thing I tested were the position values output by the C++ program. I found that the top of the camera corresponded to a value of around 22 and the bottom of the screen evaluated to around 91. To make sure it would be easy to go to the top or bottom of the screen, I set 30 to be the 0 point and 80 to be the max point. This gives the paddle 50 unique positions between the top and bottom of the screen. With a height of 480 pixels, that is roughly 10 pixels per move. Although this seems like a lot, in practice the paddle did not seem all that jerky.

Next I tested the socket to make sure it was sending data correctly. I was trying to send a char of data but I could only get the socket to successfully send an int. This would normally not be a problem, but it was sending the char as the most significant 8 bits, this resulted in a number that looked like garbage initially, but actually contained the correct data. I simply bitshifted the value right by 24 and the output value worked.

I also played a few games to be able to gauge how effective the motion control was to play the game.

##Results and Discussion
![](https://raw.githubusercontent.com/SKrupa/E190u-Lab6/master/Screenshot%20from%202015-03-10%2022_32_31.png)
![](https://raw.githubusercontent.com/SKrupa/E190u-Lab6/master/Screenshot%20from%202015-03-10%2022_32_43.png)
![](https://raw.githubusercontent.com/SKrupa/E190u-Lab6/master/Screenshot%20from%202015-03-10%2022_32_58.png)
In the above pictures there are 4 windows, the top left is stdout for the C++ program, the bottom left is stdout for the python, the top right is the game of pong as controlled by the hand in the bottom right. We can see that the outputs of both programs match, thus the correct data is being sent. We also see that the position of the paddle matches the position of the hand in the camera's view.
##Conclusion
