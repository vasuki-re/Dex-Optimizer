#!/system/bin/sh
MODDIR=${0%/*}

chmod 755 "$MODDIR/binary/dex_optimizer"

$MODDIR/binary/dex_optimizer &