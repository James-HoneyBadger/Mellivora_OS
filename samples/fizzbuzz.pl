#!/usr/bin/perl
# fizzbuzz.pl - FizzBuzz in Perl for Mellivora OS
my $i = 1;
while ($i <= 30) {
    if ($i % 15 == 0) {
        print "FizzBuzz\n";
    } elsif ($i % 3 == 0) {
        print "Fizz\n";
    } elsif ($i % 5 == 0) {
        print "Buzz\n";
    } else {
        print "$i\n";
    }
    $i++;
}
