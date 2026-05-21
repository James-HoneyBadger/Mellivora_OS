#!/usr/bin/perl
# http_get.pl - HTTP/1.1 request builder and response parser demo
# Demonstrates URL parsing, header formatting, and chunked response parsing.
# On Mellivora, use the 'curl' or 'wget' programs for actual HTTP requests.
# Run: perl http_get.pl

# ---- URL parser -------------------------------------------------------
# Parse "http://host[:port]/path" into components
sub parse_url {
    my ($url) = @_;
    my %parts = (scheme => '', host => '', port => 80, path => '/');

    # Extract scheme
    if ($url =~ /^(\w+):\/\/(.+)$/) {
        $parts{scheme} = $1;
        $url = $2;
    }

    # Extract host[:port] and path
    if ($url =~ /^([^\/]+)(\/.*)?$/) {
        my $hostport = $1;
        $parts{path} = defined($2) ? $2 : '/';

        if ($hostport =~ /^(.+):(\d+)$/) {
            $parts{host} = $1;
            $parts{port} = int($2);
        } else {
            $parts{host} = $hostport;
        }
    }

    return %parts;
}

# ---- HTTP request builder --------------------------------------------
sub build_get_request {
    my ($host, $port, $path) = @_;
    my $req = "GET $path HTTP/1.1\r\n";
    $req   .= "Host: $host";
    if ($port != 80) { $req .= ":$port"; }
    $req   .= "\r\n";
    $req   .= "User-Agent: Mellivora-Perl/1.0\r\n";
    $req   .= "Accept: text/html,text/plain\r\n";
    $req   .= "Connection: close\r\n";
    $req   .= "\r\n";
    return $req;
}

# ---- HTTP response parser --------------------------------------------
# Parse status line "HTTP/1.1 200 OK"
sub parse_status {
    my ($line) = @_;
    if ($line =~ /^HTTP\/(\d+\.\d+)\s+(\d+)\s+(.+)\r?$/) {
        return ($1, $2, $3);   # version, code, reason
    }
    return ('?', '0', 'Unknown');
}

# Parse a response header line "Name: Value"
sub parse_header {
    my ($line) = @_;
    $line =~ s/\r//g;
    if ($line =~ /^([^:]+):\s*(.+)$/) {
        return (lc($1), $2);
    }
    return ('', '');
}

# ---- Chunked transfer-encoding decoder -------------------------------
# Decode chunked body (simplified: handles one chunk at a time)
sub decode_chunked {
    my ($body) = @_;
    my $result = '';
    my @lines = split(/\r?\n/, $body);
    my $i = 0;
    while ($i < scalar(@lines)) {
        my $size_hex = $lines[$i];
        $size_hex =~ s/\s.*//;   # strip chunk extensions
        my $size = hex($size_hex);
        last if $size == 0;      # final chunk
        $i++;
        if ($i < scalar(@lines)) {
            $result .= substr($lines[$i], 0, $size);
        }
        $i++;
    }
    return $result;
}

# ---- Content-Length extractor ----------------------------------------
sub content_length {
    my (%headers) = @_;
    return exists $headers{'content-length'} ? int($headers{'content-length'}) : -1;
}

# ---- Demo ------------------------------------------------------------
my @test_urls = (
    'http://example.com/index.html',
    'http://api.honey-badger.org:8080/status',
    'http://files.mellivora.local/kernel.bin',
);

print "=== HTTP GET Request Builder Demo ===\n\n";

foreach my $url (@test_urls) {
    print "URL: $url\n";

    my %parts = parse_url($url);
    print "  Scheme : $parts{scheme}\n";
    print "  Host   : $parts{host}\n";
    print "  Port   : $parts{port}\n";
    print "  Path   : $parts{path}\n";

    my $request = build_get_request($parts{host}, $parts{port}, $parts{path});
    print "  Request:\n";
    # Print request lines with indentation
    foreach my $line (split(/\r\n/, $request)) {
        print "    | $line\n";
    }
    print "\n";
}

# ---- Simulated response parsing --------------------------------------
print "=== Response Parser Demo ===\n\n";

my $fake_response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 42\r\nX-Powered-By: Mellivora\r\n\r\n<html><body>Hello from Mellivora!</body></html>";

my @resp_lines = split(/\r\n/, $fake_response);
my ($ver, $code, $reason) = parse_status($resp_lines[0]);
print "Status : $code $reason (HTTP/$ver)\n";
print "Headers:\n";

my %resp_headers;
my $body_start = 0;
for (my $i = 1; $i < scalar(@resp_lines); $i++) {
    if ($resp_lines[$i] eq '') {
        $body_start = $i + 1;
        last;
    }
    my ($hname, $hval) = parse_header($resp_lines[$i]);
    if ($hname ne '') {
        $resp_headers{$hname} = $hval;
        print "  $hname: $hval\n";
    }
}

my $body = join("\r\n", @resp_lines[$body_start .. $#resp_lines]);
my $clen = content_length(%resp_headers);
print "Content-Length: $clen\n";
print "Body: $body\n\n";

# ---- Chunked decoding demo -------------------------------------------
print "=== Chunked Transfer Decoding ===\n\n";

my $chunked = "1a\r\nHello from chunk one!   \r\n0d\r\nSecond chunk.\r\n0\r\n\r\n";
my $decoded = decode_chunked($chunked);
print "Decoded: $decoded\n";
