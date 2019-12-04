## PGP keys of Gitian builders and Developers

The file `keys.txt` contains fingerprints of the public keys of Gitian builders
and active developers.  These keys reflect the keys used to sign git commits by
the developers.  The most recent version of each pgp key can be found on most
pgp key servers.

Fetch the latest version from the key server to see if any key was revoked in
the meantime.

To fetch the latest version of all pgp keys in your gpg homedir, use the following command:

```sh
gpg --refresh-keys
```

To fetch the developer keys, feed the keys.txt file into the following script in order
to pull the keys from the keyservers:

```sh
while read fingerprint keyholder_name; do gpg --keyserver hkp://subset.pool.sks-keyservers.net --recv-keys ${fingerprint}; done < ./keys.txt
```

Developers for Veil should make sure they are signing their commits with their public
key, and ensure the key is what is also loaded into your https://keybase.io profile.  When your
keys are publicly uploaded, you may add your key to the list here.

To verify commits after loading fetching the developer keys into your gpg keychain, you
will be able to use the git tools, such as `git log --oneline --show-signature`.

These keys can be verified with the keybase identity for the developer (found from the
Veil Team page https://veil-project.com/team/)

We encourage developers and community members to sign and trust the keys in this file to
further build the validation of indenties.
