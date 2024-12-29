# Troubleshooting
- check ip mactches the totally not hardcoded one in `Pinger.hpp` with
something like `ip addr`
- using `fat12` for the FS, just seems to work better (probably because less
important blocks as there are no inodes)
