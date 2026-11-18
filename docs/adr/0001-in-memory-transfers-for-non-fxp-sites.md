# In-memory transfers for non-FXP sites

Some sites cannot do FXP. For site-to-site transfers initiated from the split browse view (`t`), we route the bytes through the client's RAM when either the source or the target site has the new `inmemorytransfer` site flag set: the file is downloaded fully into memory, uploaded from memory, then freed — per file, sequentially, never touching local disk. True chunk streaming was rejected as a second, more complex pump mechanism with no user-visible gain; the whole spread-job/race machinery is out of scope and still requires FXP-capable sites.

## Consequences

- Peak RAM per transfer equals the size of the largest single file; files larger than a fixed 4 GiB cap fail (and eventually skip) rather than falling back to disk — no disk writes is a hard constraint.
- FXP is never auto-detected as broken: the flag is a manual declaration, and setting it forces the memory path even when FXP would have worked.
- In-memory transfers reuse the existing transfer-job plumbing (new pending-transfer type) and inherit its retry, abort, reset and status UI.
