use v5.14;
use strict;
use warnings;

my @prime = map $_, 0..4800;
@prime = grep { 2 <= $_; } @prime;

my $num = $prime[0];
while ( $num != $prime[-1] ) {
    @prime = grep {
        ($_ <= $num) or (($_ % $num) != 0);
    } @prime;

    $num = bigger_number( $num, \@prime );
}

# while ( my @numbers = splice(@prime, 0, 8) ) {
#     say join( ',', map sprintf("%5d", $_), @numbers );
# }

{
    my $fs = 48_000;
    my $param = 8;
    my $n = ($param / 340) * $fs; # 音速: 340m/s
    
    my $delay0 = bigger_number(int($n), \@prime);
    my $delay1 = bigger_number(int($delay0 * 17/13), \@prime);
    my $delay2 = bigger_number(int($delay0 * 19/13), \@prime);
    my $delay3 = bigger_number(int($delay0 * 23/13), \@prime);

    printf("%4d, %4d, %4d, %4d\n",
        $delay0, $delay1, $delay2, $delay3);

    for my $i ( 1..64 ) {
        my $t = $i / 64;
        my $time_sec = 2 ** (-1.0 + (4.0 * $t));
        my $samples = int($fs * $time_sec);

        my @times_list = map {
            $samples / $_;
        } ( $delay0, $delay1, $delay2, $delay3 );

        printf("  { %s }, // [%2d] %.3f\n",
            join(', ', map {
                my $gain = 2 ** (log2(0.001) / $_);
                sprintf("0x%03X", int(($gain * 0x1000) + .5));
            } @times_list), $i, $time_sec );
    }
}

sub bigger_number {
    my ( $th, $numbers ) = @_;
    my @tmp = grep { $th < $_; } @{$numbers};
    return @tmp ? $tmp[0] : $th;
}

sub log2 {
    return log($_[0]) / log(2.0);
}
