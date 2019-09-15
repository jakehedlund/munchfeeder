# munchfeeder
Automatic, WiFi enabled, dead simple, cat feeder. [The cat's nickname is Munch. Hence the name.]

Intentionally simply designed. Could it have a camera? Yes. Could it have used a fancy AngularJS frontend running on a pi with a 5MB homepage? Yes. Does it? No. Because I just want to feed the cat dammit. 

This project was designed to be quick and easy to build <sup>(only took me 1.5 years, shhh)</sup>, and to require almost no specialized knowledge - anyone can do it! 

# Description 

The feeder itself is made of wood and constructed using basic shop tools (table saw, router, drill). This repo is not designed to be a tutorial; however, with a little intelligence and investigation, it should be relatively simple to recreate for yourself. The SolidWorks models are included as well, for anyone who is inclined. 

* The CPU/controller is the $8 ESP8266 off Amazon, and uses Arduino-compatible code. 
* The the motor is a $20 stepper also off Amazon (in hindsight a geared-down dc motor would have been better. Torque requirements are relatively high). 
* The motor controller is a simple L298-based PCB from, you guessed it, Amazon. 

# Pics
Pics and renders can be found in the pics and renders folder (surprise!). 

Here's the final product: 

![feeder1](/pics/final2.jpg)

And the very simple web interface:

![simple_web](/pics/Feed%20the%20Munch.png)

A render of the final design, exploded view: 

![exp](/renders/exp.JPG)
