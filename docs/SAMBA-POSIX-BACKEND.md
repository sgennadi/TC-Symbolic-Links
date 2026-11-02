# Optional Samba POSIX directory links over restricted SSH

Status: opt-in backend, not an SMB protocol capability. This does not
change smb.conf, enable wide links, edit ACLs, install a Windows service or
create a .lnk shortcut. Ordinary Windows SMB and file links continue through
the Windows native API. The single TC ZIP contains no CMD, PS1 or extra EXE.

## Supported use

When Windows CreateSymbolicLinkW returns Access Denied (5), Not Supported,
Invalid Function or Privilege Not Held for a DIRECTORY on a configured
SMB share, the plugin can request a real Linux POSIX symlink on that server.
Link and source MUST be within the SAME share. Server creates a RELATIVE symlink.

This feature requires explicit permission to run a restricted SSH helper.
It is not automatically enabled just because the SMB server is Samba.

## Configure Linux server (administrator approval required)

1. Identify the real share root from smb.conf and testparm -s.
   Do not guess its location from the UNC share name.
2. Place server/tc-posix-symlink-helper.py into a root-controlled directory,
   for example /usr/local/libexec/tc-posix-symlink-helper.py (mode 755).
   Use existing SSH, Python 3 and a NON-ROOT user with preexisting share
   permissions; never grant new rights automatically.
3. Authorize an SSH public key for that account with the OpenSSH
   authorized_keys forced-command restriction. Example for the independently
   confirmed mapping \\fedora\Test -> /raid/Test:

   restrict,command="/usr/bin/python3 /usr/local/libexec/tc-posix-symlink-helper.py --share Test --root /raid/Test" ssh-ed25519 AAAA... tc-posix-only

   Replace AAAA... with the real public key; do not use this placeholder.
   The server administrator must review account, ownership and share path.
   No sudo, root login or Samba configuration changes are required.

The client always invokes the fixed remote command tcs-symlink-v1; the SSH
forced command replaces it with the helper. Without the correct authorized
command, the request must fail. It never uploads scripts or builds shell
commands from a file path.

## Configure Windows per share (explicit user opt-in)

Windows requires the optional OpenSSH client:
%SystemRoot%\System32\OpenSSH\ssh.exe

Independently verify the SSH server fingerprint and record it in the user's
known_hosts. The client uses strict host-key checking and noninteractive
key authentication. No passwords are stored or requested.

Create: %APPDATA%\TCSymbolicLinks\posix-shares.ini

Example (replace SSHUser with the real restricted server-side login):

[\\fedora\Test]
Enabled=1
SSHHost=fedora
SSHUser=tcslinks
SSHPort=22

The section is an actual UNC SHARE ROOT, not a drive letter.
Each share requires its own explicit profile and correct server mapping.
Set Enabled=0 (or remove the section) to disable all automatic fallbacks for it.

## Security and behavior

- The Windows API is always tried first. File symlinks never use POSIX fallback.
- Neither source nor link may cross the share root; no local/external paths.
- Share name in request must match the server's fixed --share argument.
- Relative path components may not be dot, dot-dot, empty or absolute;
  source/link parents are opened with dir_fd and O_NOFOLLOW.
- Atomic os.symlink prevents overwrites. Linux lstat/readlink verifies it.
- SSH uses StrictHostKeyChecking=yes and BatchMode=yes, no password prompt,
  no forwarding, no terminal and no command interpreter on Windows.
- This backend never changes Samba settings or permissions.
- A valid POSIX symlink may appear as a normal directory over SMB:
  a Windows REPARSE TAG is NOT required for POSIX link validation.
- Samba's existing symlink traversal rules still determine whether Windows
  clients can follow a valid POSIX directory link.
- Errors are reported without falling back to .lnk shortcuts or fake files.

## Test

On Linux:
    python3 -m unittest discover -v -s tests -p test_posix_helper.py

Compile the Windows client with VS 2026 x86/x64 and run its native tests.
A passing Linux unit test is not proof of remote integration; manual testing
with an authorized Samba server must be completed before a stable release.
