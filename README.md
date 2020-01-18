# Munch Feeder

Automatic, WiFi enabled, dead simple, cat feeder. [The cat's nickname is Munch. Hence the name Munch Feeder.]

Intentionally simply designed. Could it have a camera? Yes. Could it have used a fancy AngularJS frontend running on a pi with a 5MB homepage? Yes. Does it? No. Because I just want to feed the cat dammit.

This project was designed to be quick and easy to build <sup>(only took me 1.5 years, shhh)</sup>, and to require almost no specialized knowledge - anyone can do it!

Imgur album with commentary/explanations: <https://imgur.com/a/swiDUQT>

## Description

The feeder itself is made of wood and constructed using basic shop tools (table saw, belt sander, drill/drill-press). This repo is not designed to be a tutorial; however, with a little intelligence and planning, it should be relatively simple to recreate for yourself. The SolidWorks models and drawings are included as well, for anyone to reproduce the design.

* The CPU/controller is the $8 ESP8266 off Amazon, and uses Arduino-compatible code. (<https://www.amazon.com/gp/product/B010O1G1ES/>)
* The the motor is a $20 stepper also off Amazon (in hindsight a geared-down dc motor would have been better. Torque requirements are relatively high). (<https://www.amazon.com/gp/product/B071LTXQGW>)
* The motor controller is a simple L298-based PCB from, you guessed it, Amazon. (<https://www.amazon.com/gp/product/B014KMHSW6/>)

The design is simple enough that most parts/dimensions can be changed out without too much trouble. Want a larger capacity? Just make it taller. Want it narrower? Easy. Want to add a camera and use a raspberry pi to host the web interface? Go for it.

A final note:

The finish used was butcher block oil from Lowe's, so hopefully food safe and not harmful to cats. The oil also seals the wood in order to protect it from absorbing oils and odors from the food. The lid does a decent job of keeping the catfood smell contained in spite of no seal being used (weatherstripping was considered). The construction is rugged and heavy enough that it should be resistant to aggressive cats trying to gain access for a free meal.  

## Pics

Pics and renders can be found in the pics and renders folder, but here's an overview.

The final product:

![feeder1](/pics/final2.jpg)

And the very simple web interface:

![simple_web](/pics/Feed%20the%20Munch.png)

A render of the final design, exploded view: 

![exp](/renders/exp.JPG)
