t <- seq(0,8*pi,.01)
w <- pnorm(t, 4*pi, .7)
n <- length(t)
pitchDiff <- 6
sin1 <- sin(t) + pitchDiff
sin2 <- sin(t)

pad <- 3
lineWidth <- 15
outlineWidth <- 20
sz <- 200

png("tonyLogoWave.png", width = sz, height = sz, bg="transparent")
par(mar=c(0,0,0,0))
plot(t, sin1 * w + sin2 * (1-w), type='l', 
     lwd=lineWidth, bty='n', xaxt='n', yaxt='n',
     ylim = c(-pad, pitchDiff+pad) + c(1,-1)*0.1,
     xlim = c(0, 8*pi) + c(1,-1)*0.1,
     col="white")
plot(t, sin1 * w + sin2 * (1-w), type='l', 
     lwd=lineWidth, col = blacj)
lines(c(0,0),c(-pad,pitchDiff+pad), lwd=outlineWidth, col = 'blue', lend=2)
lines(c(0,0)+8*pi,c(-pad,pitchDiff+pad), lwd=outlineWidth, col = 'blue', lend=2)
lines(c(0,8*pi),c(0,0)+pitchDiff+pad, lwd=outlineWidth, col = 'blue', lend=2)
lines(c(0,8*pi),c(0,0)-pad, lwd=outlineWidth, col = 'blue', lend=2)
dev.off()