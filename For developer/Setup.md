Setting Up the Nero Development Environment
This guide provides detailed instructions for setting up your system to explore or study the Nero programming language, developed by Arinara Network Studio.

Note: The stable version of Nero may include changes to compilers or setup requirements. Always verify with the source code.

Prerequisites
To work with Nero, you’ll need:

Operating System:
Linux, macOS, or Android (Termux recommended for mobile).


Tools:
C compiler: clang or gcc.
Git: For cloning the repository.
Text editor: VS Code, Vim, Emacs, or similar.


Dependencies (Android/Termux):
Install via pkg install clang git make.



System Requirements



Component
Minimum Requirement



OS
Linux, macOS, Android 8.0+ (Termux)


Storage
500 MB free space


RAM
2 GB (4 GB recommended for compilation)


Setup Steps

Clone the Repository:
Download the Nero project:git clone https://github.com/buchorim/Nero.git
cd Nero




Verify Tools:
Check that clang and git are installed:clang --version
git --version




Compile a Stable Compiler:
Navigate to a directory containing a stable compiler.
Build the compiler:clang compiler.c -o nero_compiler




Test a Nero Program:
Create a file test.nero:Pake Nero
Tampilkan "Halo, dunia!"
(Jalan)


Compile and run:./nero_compiler test.nero
./a.out


Expected output: Halo, dunia!


Debugging (Optional):
Use the --debug flag for detailed logs:./nero_compiler test.nero --debug





Troubleshooting

Compiler Fails:
Update packages: pkg update && pkg upgrade (Termux) or equivalent.
Ensure clang is compatible with your system.


Permission Errors:
Run chmod +x a.out before executing the binary.


Missing Dependencies:
Install required tools as listed in the prerequisites.




Reminder: The stable version may use different compilers or configurations. Check the source code for the latest setup details.

Next Steps

Learn Code Structure: Read the development guide on code organization.
Explore Tutorials: Try example programs in the tutorials folder.
Study Documentation: Understand Nero’s design through the documentation folder.

This setup enables you to run and study Nero’s compilers, offering a hands-on way to explore its experimental design.
