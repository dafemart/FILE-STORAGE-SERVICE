// $Id: cix.cpp,v 1.4 2016-05-09 16:01:56-07 - - $

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

#include "protocol.h"
#include "logstream.h"
#include "sockets.h"

logstream log (cout);
struct cix_exit: public exception {};
vector<string> parse_line(const string& line, const char& delimiter);
void initialize_char_arr(char arr[], string initializer);
static int command_index = 0;
static int argument_one = 1;


unordered_map<string,cix_command> command_map {
   {"exit", cix_command::EXIT},
   {"help", cix_command::HELP},
   {"ls"  , cix_command::LS  },
   {"get" , cix_command::GET },
   {"put" , cix_command::PUT },
   {"rm"  , cix_command::RM  },
};

void cix_help() {
   static const vector<string> help = {
      "exit         - Exit the program.  Equivalent to EOF.",
      "get filename - Copy remote file to local host.",
      "help         - Print help summary.",
      "ls           - List names of files on remote server.",
      "put filename - Copy local file to remote host.",
      "rm filename  - Remove file from remote server.",
   };
   for (const auto& line: help) cout << line << endl;
}

void cix_ls (client_socket& server) {
   cix_header header;
   header.command = cix_command::LS;
   log << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   log << "received header " << header << endl;
   if (header.command != cix_command::LSOUT) {
      log << "sent LS, server did not return LSOUT" << endl;
      log << "server returned " << header << endl;
   }else {
      char buffer[header.nbytes + 1];
      recv_packet (server, buffer, header.nbytes);
      log << "received " << header.nbytes << " bytes" << endl;
      buffer[header.nbytes] = '\0';
      cout << buffer;
   }
}

void cix_get(client_socket& server, const string& filename){
    cix_header header;
    if(filename.size() > FILENAME_SIZE)
       throw  socket_error("invalid filename size");
    header.command = cix_command::GET;
    initialize_char_arr(header.filename, filename);
    log << "sending header" << header << endl;
    send_packet(server, &header, sizeof header);
    recv_packet (server, &header, sizeof header);
    log << "received header" << header << endl;
    if(header.command != cix_command::FILE){
       log << "sent GET, server did not return FILE" << endl;
       log << "server returned" << header << endl;
    }else{
      char buffer[header.nbytes];
      recv_packet(server,buffer,header.nbytes);
      log << "received " << header.nbytes << " bytes " << endl;
      FILE* fs;
      fs = fopen(filename.c_str(), "wb" );
      if((fwrite(&buffer, sizeof(char), header.nbytes, fs))
          != header.nbytes) 
          log << "there was a problem writing into the file" << endl;
      fclose(fs); 
   }
   log << "sucessfull delivery -- exiting get" << endl;
}

void cix_put(client_socket& server, const string& filename){
    FILE *fs;
    fs = fopen(filename.c_str(), "rb");
    if(fs == nullptr){
       log << "no such file" << endl;
       return;
    }
    string file_output;
    char buffer[0x1000];
    size_t buffer_size = 4096;
    size_t characters_read{0};
    while((characters_read = fread(&buffer,
          sizeof(char), buffer_size, fs)) == buffer_size ){
        log << "buffering" << endl;
        file_output.append(buffer);
    }
    buffer[characters_read] = '\0';
    file_output.append(buffer);
    fclose(fs);
    cix_header header;
    header.command = cix_command::PUT;
    header.nbytes = file_output.size();
    initialize_char_arr(header.filename, filename);
    log << "sending header" << header << endl;
    send_packet(server, &header, sizeof header);
    send_packet(server, file_output.c_str(), file_output.size());
    log << "sent" << file_output.size() << "bytes" << endl;
    recv_packet(server, &header, sizeof header);
    if(header.command != cix_command::ACK){
       log << "sent PUT, server did not return ACK" << endl;
       log << "server returned" << header << endl;
    }
    else log << "sucessfull delivery -- exiting put" << endl;
}

void cix_rm(client_socket& server, const string& filename){
    cix_header header;
    header.command = cix_command::RM;
    header.nbytes = 0;
    initialize_char_arr(header.filename, filename);
    log << "sending header" << header << endl;
    send_packet(server, &header, sizeof header);
    recv_packet(server, &header, sizeof header);
   if(header.command != cix_command::ACK){
     log << "sent PUT, server did not return ACK" << endl;
     log << "server returned" << header << endl;
   }
   else log << "sucessfull delivery -- exiting rm" << endl;
}

void usage() {
   cerr << "Usage: " << log.execname() << " [host] [port]" << endl;
   throw cix_exit();
}

int main (int argc, char** argv) {
   vector<string> parsed_line;
   log.execname (basename (argv[0]));
   log << "starting" << endl;
   vector<string> args (&argv[1], &argv[argc]);
   if (args.size() > 2) usage();
   string host = get_cix_server_host (args, 0);
   in_port_t port = get_cix_server_port (args, 1);
   log << to_string (hostinfo()) << endl;
   try {
      log << "connecting to " << host << " port " << port << endl;
      client_socket server (host, port);
      log << "connected to " << to_string (server) << endl;
      for (;;) {
         string line;
         getline (cin, line);
         if (cin.eof()) throw cix_exit();
         log << "command " << line << endl;
         parsed_line = parse_line(line,' ');
         const auto& itor =
         command_map.find (parsed_line[command_index]); //--
         cix_command cmd = itor == command_map.end()
                         ? cix_command::ERROR : itor->second;
         switch (cmd) {
            case cix_command::EXIT:
               throw cix_exit();
               break;
            case cix_command::HELP:
               cix_help();
               break;
            case cix_command::LS:
               cix_ls (server);
               break;
            case cix_command::GET:
               cix_get(server , parsed_line[argument_one]);
                break;
            case cix_command::PUT:
                 cix_put(server, parsed_line[argument_one]);
                 break;
            case cix_command::RM:
                 cix_rm(server, parsed_line[argument_one]);
                 break;
            default:
               log << line << ": invalid command" << endl;
               break;
         }
      }
   }catch (socket_error& error) {
      log << error.what() << endl;
   }catch (cix_exit& error) {
      log << "caught cix_exit" << endl;
   }
   log << "finishing" << endl;
   return 0;
}

vector<string> parse_line(const string& line, const char& delimiter){
    string container;
    vector<string> parsed_line;
    for(auto& it : line){
       if(it == delimiter){
          parsed_line.push_back(container);
          container.clear();
       }
       else container += it;
    }
    if(container.empty() != true)
       parsed_line.push_back(container);
    return parsed_line;
} 

void initialize_char_arr(char arr[], string initializer){
      int position = 0;
      for(auto& it : initializer){
         arr[position] = it;
         position++;
      }
      arr[position] = '\0';
}
