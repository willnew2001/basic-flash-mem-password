# basic-flash-mem-password

This project uses a flash memory device that communicates with the 1-wire protocol.

The program just stores a user-entered password in the flash memory which persists on restart.
Subsequent executions of the program prompt the user to reenter the password, upon which the
user has "logged in".

This program was written on a Raspberry Pi 4b running an installation of Plan9 for a university 
class final project. Due to the OS being Plan9, the version of C written is slightly different 
than traditional. You might notice 'print' instead of 'printf', for example. You will not be 
able to compile this code using gcc.

I don't have the link to the flash memory device since it was provided by the professor.
