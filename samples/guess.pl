#!/usr/bin/perl
# guess.pl - Number guessing game in Perl for Mellivora OS

my $secret = rand(100) + 1;
my $tries = 0;

print "I'm thinking of a number between 1 and 100.\n";

while (1) {
    print "Your guess: ";
    my $guess = <STDIN>;
    chomp($guess);
    $tries++;

    if ($guess == $secret) {
        print "Correct! You got it in $tries tries!\n";
        last;
    } elsif ($guess < $secret) {
        print "Too low!\n";
    } else {
        print "Too high!\n";
    }
}
