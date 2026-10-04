# Shell completions

The bash, zsh and fish completions are not checked in: the build generates them
from the CLI's own option and verb tables with

    tether --print-completions bash   # or zsh, fish

and installs them to the usual places (`bash-completion/completions/tether`,
`zsh/site-functions/_tether`, `fish/vendor_completions.d/tether.fish`). See
`completions.cmake`, which the top-level `CMakeLists.txt` includes. A portable
build can run the same command and drop the output wherever its shell looks.
