mkdir -p matrices && cd matrices
for gm in McRae/ecology2 Bourchtein/atmosmodd \
          LPnetlib/lp_wood1p Norris/heart1 ; do
  name=${gm##*/}
  curl -L "https://sparse.tamu.edu/MM/$gm.tar.gz" | tar xz   # -> <name>/<name>.mtx
  mv "$name/$name.mtx" . && rm -rf "$name"                   # flatten to matrices/<name>.mtx
done