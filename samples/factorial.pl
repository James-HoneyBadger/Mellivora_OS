#!/usr/bin/perl
# factorial.pl - Factorials in Perl for Mellivora OS

sub factorial {
    my $n = $ARGV[0];
    if ($n <= 1) {
        return 1;
    }
    my $result = 1;
    my $i = 2;
    while ($i <= $n) {
        $result = $result * $i;
        $i++;
    }
    return $result;
}

foreach my $n (1..12) {
    $result = 1;
    $i = 2;
    while ($i <= $n) {
        $result = $result * $i;
        $i++;
    }
    print "$n! = $result\n";
}
