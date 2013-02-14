Echse
=====

Event state stream tools.

Vapourware.

Formalities
-----------
Let `I` be an instant and `S = {s1, ..., sn}` a fixed set of streams,
which map an instant to an instant.

    i: instant
    S := {s1, ..., sn} with sj(i) =: ij and ij > i and sj(ij) > ij
    
    repeat
      compute I' := S(i) := {s1(i), ..., sn(i)} = {ij1, ..., ijn}`
      choose i' := min(I')
      set i <- i'

This will yield a sequence of instants, `i, i', i'', ...` in strict
chronological order, just like the sequence `i, sj(i), sj(sj(i)), ...`
and we shall define:

    S(k) = (k)' for the element (k) of i > i' > i'' > ... >= (k) > (k)'

which then in turn fulfills the definition above of a stream.

So for a second set of streams `S' = {s'1, ..., s'm}` we could instead
of running the union of sets `S` and `S'` cascade the sets like so:

    C = {s'1, ..., s'm, S} 

Now, consider streams that map instants to a tuple `[instant, +/-1]`
where `+1` denotes entering a state, and `-1' denotes leaving a state.

The sequence of tuples we get when running the routine above (with the
obvious modifications) is exactly what's written in sss files.

