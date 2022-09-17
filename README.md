# hd-idle-for-windows -  a hard disk idle spin-down utility
A Windows port of the hd-idle classic, originally written by Christian Müller in 2007.

The source code looks a bit old fashioned, but I did not want to refactor it according to
modern taste. There are only two source files:
- hd-idle.cpp
- getopt.cpp

Both file extensions are cpp, but in fact everything is written in C.

The application needs to run in a console window with administrative permissions, as it needs to
access the physical drives itself.

I am using it in my home server to spin-down three WD-Red HDDs. These HDDs are used as mass storage
for backups, etc. As the server is running 24/7, spinning down the HDDs saves quite some energy.

It has been tested to run on:
- Hyper-V Core 2012R2
- Windows 10

In my setup all HDDs are mounted to a Hyper-V Core 2012R2 host. The HDDs hold vhd(x) files, that are
mounted by the guest VMs. hd-idle-for-windows is started and permanently running in a console window
on the Hyper-V host.

When started without any parameters it will probe all physical drives in 6s intervals. When
there is still read or write actitivity, the corresponding read or write counters of the OS
will increment. This is an indication that the drive is in use. If there is no drive activity
for 60s, the drive is spin-down. When the drive is accessed after spin-down, either through 
windows explorer or some file access, the OS will automatically spin-up the drive. This may 
take a few seconds, so there is a delay, depending on the spin-up time of the drive.

A word of caution: hard disks don't like spinning up too often. Laptop disks
are more robust in this respect than desktop disks but if you set your disks
to spin down after a few seconds you may damage the disk over time due to the
stress the spin-up causes on the spindle motor and bearings. It seems that
manufacturers recommend a minimum idle time of 3-5 minutes, the default in
hd-idle is 10 minutes.

As always, use it at your own risk.

