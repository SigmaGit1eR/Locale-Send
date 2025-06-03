Remote File/Folder Deployment Tool
===================================

Description
-----------
This C++ program copies a selected EXE file or folder to remote computers via their IP addresses,
using Windows Admin Share (C$). It creates a shortcut (.lnk) on the remote desktop
(C:\Users\Public\Desktop), which allows the copied executable or folder to be launched easily.

This version does NOT modify the registry in any way.

Features
--------
- Automatically retrieves a list of IP addresses from the "arp -a" command
- Prompts the user to select a file or folder using a native Windows file dialog
- Determines whether the selected path is a file or a folder
- Connects to the remote computer's C$ share using default administrative credentials
- Copies the file or folder to the remote C:\ drive
- Creates a desktop shortcut (.lnk) on the remote machine
- Disconnects after each operation
- English interface and full inline comments for developers

System Requirements
-------------------
- Windows 10 or later
- Administrator privileges (required for access to C$ shares)
- Remote PCs must have administrative shares enabled (C$)
- The same credentials must be valid on the remote systems

Usage Instructions
------------------
1. Run the application as Administrator.
2. Select the file or folder you want to copy.
3. The program will:
   - Get the IPs using "arp -a"
   - Attempt to connect to each IP's C$ share
   - Copy the selected file/folder to C:\
   - Create a shortcut on the public desktop
4. A success or error message will be shown for each attempt.

Notes
-----
- If the file already exists on the remote system, it will be overwritten.
- If the desktop shortcut already exists, it will be overwritten.
- If you are deploying a folder, it will use XCOPY with flags: /E /I /Y
- Remote computers must be reachable via network and powered on.

Developer Notes
---------------
This project uses:
- Windows Shell COM interfaces (IShellLink, IPersistFile)
- Windows networking functions (WNetAddConnection2W, WNetCancelConnection2W)
- Standard filesystem and command execution APIs
- No external libraries or CMake required

Security Notice
---------------
This tool assumes you have permission to access and deploy files to remote computers.
Using it without authorization may violate laws or organizational policies.

Author
------
Created by: SigmaGit1er  
Language: C++ (Windows API)
