d=read.table("compressed_size_versus_uncompressed_length-o4-t100-wv.csv",sep=";",header=TRUE)
plot(d$minlength,d$compressed_size_in_percent,xlim=c(0,140),type="b",ylab="Size of compressed message (as % of uncompressed size)",xlab="Length of uncompressed message (in characters)",main="Compression Performance versus Message Length")

e=read.table("compressed_size_hist-o4-t100-wv.csv",sep=";",header=TRUE)
barplot(names.arg=e$compressed_size_in_percent,height=e$count)
