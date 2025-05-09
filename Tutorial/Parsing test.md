Nero Parsing Experiments
This document collects notes and examples from parsing experiments conducted during the development of the Nero programming language.

Note: The stable version of Nero may include different parsing behaviors or features. Verify with the source code.

Purpose
Nero’s natural language-inspired syntax, such as Tanyakan "Siapa namamu?" simpan ke nama, required innovative parsing techniques to handle its unique commands. These experiments, conducted by Arinara Network Studio, tested various approaches to ensure accurate and efficient compilation. This repository is a final archive, but the stable version may reflect changes.
Experiment Goals
The parsing tests focused on:

Syntax Validation: Ensuring commands like Pake Nero and (Jalan) are correctly interpreted.
Command Processing: Handling Nero’s intuitive constructs (e.g., Tampilkan, Atur, Tanyakan).
Error Handling: Providing clear, user-friendly error messages for invalid syntax.

Sample Experiment
Test Case: Basic Program Parsing

Input Program:Pake Nero
Atur angka 42
Tampilkan "Jawabannya adalah " gabung angka
(Jalan)


Goal: Verify that the parser processes variable assignment and output commands.


Setup:
Use a stable compiler:clang compiler.c -o nero_compiler
./nero_compiler test.nero --debug




Observations:
The parser correctly identified Atur and Tampilkan.
Debug logs showed proper variable storage for angka.
Output: Jawabannya adalah 42.



Results



Component
Status
Notes



Syntax Validation
Success
Recognized Pake Nero and (Jalan)


Variable Parsing
Success
Stored angka correctly


Output Generation
Success
Printed concatenated string


Additional Test
Test Case: Error Handling

Input Program:Pake Nero
Tampilkan "Halo" gabung nama
(Jalan)


Goal: Check error reporting for undefined variable nama.


Result:
Error: compiler.c:123: error: Undefined variable 'nama'.
The parser halted gracefully, freeing resources.




Reminder: The stable version may handle parsing differently. Check the source code for the latest behavior.

Using These Notes
To replicate or study these experiments:

Set Up: Follow the setup guide in the development guides folder.
Choose a Compiler: Use a stable compiler for consistent results.
Run Tests: Create test programs and use the --debug flag to observe parsing.

These notes provide a detailed look at Nero’s parsing challenges and solutions, offering valuable insights for compiler enthusiasts.
