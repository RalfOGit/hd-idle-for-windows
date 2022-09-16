# hd-idle-for-windows
A port of the hd-idle classic to windows, originally written by Christian Müller in 2007.

The source code looks a bit old fashioned, but I did not want to refactor it according to
modern taste. There are only two source files:

   hd-idle.cpp
   getopt.cpp

The extension is cpp, but it is written in C.

The application needs to run in a command window with administrative permissions, as it uses 

    HANDLE hDevice = CreateFile(name,        // drive to open
        GENERIC_READ | GENERIC_WRITE,        // read and write access to the drive
        FILE_SHARE_READ | FILE_SHARE_WRITE,  // share mode           
        NULL,                                // default security attributes
        OPEN_EXISTING,                       // disposition
        0,                                   // file attributes
        NULL);                               // do not copy file attributes
        
to directly access the drive itself.

I use it in my home server to spin-down to WDD-Red HDDs. These HDDs are used as mass storage
for my backups, etc. 

When started without any parameters it will probe all physical drives in 6s intervals. When
there is still read or write actitivity, the corresponding read or write counters of the OS
will increment. This is an indication that the drive is in use. If there is no drive activity
for 60s, the drive is spin-down. When the drive is accessed after spin-down, either through 
windows explorer or some file access, the OS will automatically spin-up the drive. This may 
take a few seconds, so there is a delay, depending on the spin-up time of the drive.

Hard Disk Idle Spin-Down Utility
==============================================================================

hd-idle is a utility program for spinning-down external disks after a period
of idle time. Since most external IDE disk enclosures don't support setting
the IDE idle timer, a program like hd-idle is required to spin down idle
disks automatically.

A word of caution: hard disks don't like spinning up too often. Laptop disks
are more robust in this respect than desktop disks but if you set your disks
to spin down after a few seconds you may damage the disk over time due to the
stress the spin-up causes on the spindle motor and bearings. It seems that
manufacturers recommend a minimum idle time of 3-5 minutes, the default in
hd-idle is 10 minutes.

