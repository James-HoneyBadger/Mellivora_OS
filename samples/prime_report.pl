#!/usr/bin/perl
# prime_report.pl - Prime number report using a reusable Perl subroutine

sub is_prime {
    my ($candidate) = @_;
    return 0 if $candidate < 2;
    for (my $factor = 2; $factor * $factor <= $candidate; $factor++) {
        return 0 if $candidate % $factor == 0;
    }
    return 1;
}

my @primes;
for (my $number = 2; $number <= 50; $number++) {
    push @primes, $number if is_prime($number);
}

print "Primes up to 50:\n";
print join(", ", @primes), "\n";
