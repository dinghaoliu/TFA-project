#include "DBTools.h"
#include <sstream>

void update_database(GlobalContext *Ctx){

    MYSQL *conn;

    //Use your own database config
    const char *server = "localhost";
    const char *user = "user";
    const char *pwd = "";
    const char *database = "icall_data";
 
    string table_name_icall = "icall_target_table";
    string table_name_caller = "caller_table";
    string table_name_func = "func_table";

    string drop_table_icall = "drop table if exists " + table_name_icall;
    string drop_table_caller = "drop table if exists " + table_name_caller;
    string drop_table_func = "drop table if exists " + table_name_func;

    string create_table_icall = "create table " + table_name_icall;
    string create_table_caller = "create table " + table_name_caller;
    string create_table_func = "create table " + table_name_func;

    create_table_icall += "( id int auto_increment, ";
    create_table_icall += "line_number int, ";
    create_table_icall += "caller_func varchar(3000), ";
    create_table_icall += "target_num int, ";
    create_table_icall += "MLTA_result varchar(20), ";
    create_table_icall += "Alias_result varchar(25), ";
    create_table_icall += "target_set_hash varchar(30), ";
    create_table_icall += "file_location varchar(500), ";
    create_table_icall += "primary key(id));";

    create_table_caller += "( id int auto_increment, ";
    create_table_caller += "func_set_hash varchar(30), ";
    create_table_caller += "func_name varchar(3000), ";
    create_table_caller += "primary key(id));";

    create_table_func += "( id int auto_increment, ";
    create_table_func += "func_name varchar(3000), ";
    create_table_func += "Alias_result varchar(25), ";
    create_table_func += "frequency int, ";
    create_table_func += "primary key(id));";

    MYSQL mysql;
    conn = mysql_init(&mysql);

    if(!mysql_real_connect(conn, server, user, pwd, database, 0, NULL, 0)){
        OP<<"WARNING: MYSQL connect failed!\n";
        OP<<mysql_error(conn);
        OP<<"\n";
        return;
    }

    //First clean the old table data
    if(mysql_query(conn, drop_table_icall.c_str())) {
        OP<<"Drop icall_target_table failed\n";
        return;
    }

    //Create a new table to record our data
    if(mysql_query(conn, create_table_icall.c_str())) {
        OP<<"Create icall_target_table failed\n";
        return;
    }

    //Insert new icall results
    vector<string> cmds;
    cmds.clear();
    build_insert_batch_for_icall_table(Ctx, 500, cmds);
    for(unsigned i = 0; i < cmds.size(); i++){
        string insert_cmd = cmds[i];
        if(mysql_query(conn, insert_cmd.c_str())) {
            OP<<"Insert icall_target_table failed\n";
            OP<<"cmd: "<<insert_cmd<<"\n";
        }
    }
    cmds.clear();

    //First clean the old table data
    if(mysql_query(conn, drop_table_caller.c_str())) {
        OP<<"Drop caller_table failed\n";
        return;
    }

    //Create a new table to record our data
    if(mysql_query(conn, create_table_caller.c_str())) {
        OP<<"Create caller_table failed\n";
        return;
    }

    //Insert new caller results
    build_insert_batch_for_caller_table(Ctx, 500, cmds);
    for(unsigned i = 0; i < cmds.size(); i++){
        string insert_cmd = cmds[i];
        if(mysql_query(conn, insert_cmd.c_str())) {
            OP<<"Insert caller_table failed\n";
            OP<<"cmd: "<<insert_cmd<<"\n";
        }
    }

    cmds.clear();

    //First clean the old table data
    if(mysql_query(conn, drop_table_func.c_str())) {
        OP<<"Drop func_table failed\n";
        return;
    }

    //Create a new table to record our data
    if(mysql_query(conn, create_table_func.c_str())) {
        OP<<"Create func_table failed\n";
        return;
    }

    //Insert new caller results
    build_insert_batch_for_func_table(Ctx, 500, cmds);
    for(unsigned i = 0; i < cmds.size(); i++){
        string insert_cmd = cmds[i];
        //OP<<"cmd: "<<insert_cmd<<"\n";
        if(mysql_query(conn, insert_cmd.c_str())) {
            OP<<"Insert func_table failed\n";
            OP<<"cmd: "<<insert_cmd<<"\n";
        }
    }
    cmds.clear();

    //Close connection
    mysql_close(&mysql);
}

//Used to speed up database insert
void build_insert_batch_for_icall_table(GlobalContext *Ctx, int batch_size, vector<string> &cmds){

    string insert_statement;

    stringstream insertss;
    insertss << "insert into icall_target_table ";
    insertss << "(line_number, caller_func, target_num, MLTA_result, Alias_result, target_set_hash, file_location) values ";

    int batchnum = 0;
    unsigned icallnum = Ctx->ICallees.size();
    unsigned icallid = 1;
    for(auto i = Ctx->ICallees.begin(); i!= Ctx->ICallees.end(); i++){

        batchnum++;
        
        CallInst* cai = i->first;
        FuncSet fset = i->second;
        unsigned lineNo = getInstLineNo(cai);
        unsigned long long num = fset.size();
        Function* caller = cai->getFunction();

        insertss << "(";
        insertss << lineNo << ",";
        insertss << "\"" <<  caller->getName().str()  << "\"" << "," << num << ",";
        switch(Ctx->Global_MLTA_Reualt_Map[cai]){
            case TypeEscape:
                insertss << "\"" << "TypeEscape"  << "\"";
                break;
            case OneLayer:
                insertss << "\"" << "OneLayer" << "\"";
                break;
            case TwoLayer:
                insertss << "\"" << "TwoLayer" << "\"";
                break;
            case ThreeLayer:
                insertss << "\"" << "ThreeLayer" << "\"";
                break;
            case NoTwoLayerInfo:
                insertss << "\"" << "NoTwoLayerInfo" << "\"";
                break;
            case MissingBaseType:
                insertss << "\"" << "MissingBaseType" << "\"";
                break;
            case MixedLayer:
                insertss << "\"" << "MixedLayer" << "\"";
                break;
            case VTable:
                insertss << "\"" << "VTable" << "\"";
                break;
            default:
                insertss << "\"" << "unknown" << "\"";
                break;
        }

        insertss << ",";

        if(Ctx->Global_Alias_Results_Map[cai] == 1){
            insertss << "\"" << "success" << "\"";
        }
        else{
            switch(Ctx->Global_ICall_Alias_Result_Map[cai]){
                case none:
                    insertss << "\"" << "No need to analysis"  << "\"";
                    break;
                case F_has_no_global_definition:
                    insertss << "\"" << "F has no global def"  << "\"";
                    break;
                case binary_operation:
                    insertss << "\"" << "Binary operation"  << "\"";
                    break;
                case llvm_used:
                    insertss << "\"" << "llvm_used"  << "\"";
                    break;
                case exceed_max:
                    insertss << "\"" << "exceed_max"  << "\"";
                    break;
                case inline_asm:
                    insertss << "\"" << "inline_asm"  << "\"";
                    break;
                case init_in_asm:
                    insertss << "\"" << "init_in_asm"  << "\"";
                    break;
                default:
                    insertss << "\"" << "unknown" << "\"";
                    break;
            }
        }

        stringstream ss;
        ss<<funcSetHash(fset);

        insertss << "," << "\"" << ss.str() << "\"";

        insertss << "," << "\"" << getInstFilename(cai)<< "\"" <<")";

        //Stop batch collection and build a new batch
        if(batchnum >= batch_size || icallid == icallnum){
            batchnum = 0;

            insertss << ";";
            cmds.push_back(insertss.str());
            insertss.str("");

            if(icallid != icallnum){
                insertss << "insert into icall_target_table ";
                insertss << "(line_number, caller_func, target_num, MLTA_result, Alias_result, target_set_hash, file_location) values ";
            }
        }
        else{
            insertss << ",";
        }

        icallid++;
        
    }//end for loop

}

//Used to speed up database insert
void build_insert_batch_for_caller_table(GlobalContext *Ctx, int batch_size, vector<string> &cmds){

    string insert_statement;

    stringstream insertss;
    insertss << "insert into caller_table ";
    insertss << "(func_set_hash, func_name) values ";

    int batchnum = 0;
    unsigned long long icallnum = 0;
    unsigned long long icallid = 1;

    map<size_t, FuncSet> funcHashMap;
    funcHashMap.clear();
    set<size_t> funcHashSet;
    funcHashSet.clear();

    for(auto i = Ctx->ICallees.begin(); i!= Ctx->ICallees.end(); i++){
        FuncSet fset = i->second;
        if(fset.empty())
            continue;

        size_t funcsethash = funcSetHash(fset);
        if(funcHashSet.count(funcsethash))
            continue;

        funcHashSet.insert(funcsethash);
        icallnum += fset.size();
    }

    funcHashSet.clear();

    for(auto i = Ctx->ICallees.begin(); i!= Ctx->ICallees.end(); i++){

        CallInst* cai = i->first;
        FuncSet fset = i->second;
        if(fset.empty())
            continue;
        
        size_t funcsethash = funcSetHash(fset);
        if(funcHashSet.count(funcsethash))
            continue;
        funcHashSet.insert(funcsethash);
        
        for(auto j = fset.begin(); j != fset.end(); j++){
            batchnum++;

            Function* f = *j;

            insertss << "(";
            stringstream ss;
            ss << funcsethash;
            insertss << "\"" << ss.str() << "\"" << ",";

            insertss << "\"" <<  f->getName().str()  << "\"" ;
            insertss << ")";

            //Stop batch collection and build a new batch
            if(batchnum >= batch_size || icallid == icallnum){
                batchnum = 0;
                insertss << ";";
                cmds.push_back(insertss.str());
                insertss.str("");

                if(icallid != icallnum){
                    insertss << "insert into caller_table ";
                    insertss << "(func_set_hash, func_name) values ";
                }
            }
            else{
                insertss << ",";
            }
            icallid++;
        }
    }//end for loop

}

size_t funcSetHash(FuncSet fset){
    
    size_t Funcsethash = 0;
    
    for(auto i = fset.begin(); i != fset.end(); i++){
        Function* f = *i;
        hash<string> str_hash;
        Funcsethash += str_hash(f->getName().str());
    }
    return Funcsethash;
}

void build_insert_batch_for_func_table(GlobalContext *Ctx, int batch_size, vector<string> &cmds){

    for(auto i = Ctx->ICallees.begin(); i!= Ctx->ICallees.end(); i++){
        
        CallInst* cai = i->first;
        FuncSet fset = i->second;
        for(auto f : fset){
            Ctx->Global_Func_Alias_Frequency_Map[f].insert(cai);
        }
    }

    string insert_statement;

    stringstream insertss;
    insertss << "insert into func_table ";
    insertss << "(func_name, Alias_result, frequency) values ";

    int batchnum = 0;
    unsigned long long icallid = 1;
    unsigned icallnum = Ctx->Global_AddressTaken_Func_Set.size();

    for(auto it = Ctx->Global_AddressTaken_Func_Set.begin(); 
        it != Ctx->Global_AddressTaken_Func_Set.end(); it++){

        Function* f = *it;

        batchnum++;
        insertss << "(";
        stringstream ss;
        insertss << "\"" <<  f->getName().str()  << "\"" << "," ;

        switch(Ctx->Global_Func_Alias_Result_Map[f]){
            case none:
                insertss << "\"" << "No need to analysis"  << "\"";
                break;
            case F_has_no_global_definition:
                insertss << "\"" << "F has no global def"  << "\"";
                break;
            case binary_operation:
                insertss << "\"" << "Binary operation"  << "\"";
                break;
            case llvm_used:
                insertss << "\"" << "llvm_used"  << "\"";
                break;
            case exceed_max:
                insertss << "\"" << "exceed_max"  << "\"";
                break;
            case inline_asm:
                    insertss << "\"" << "inline_asm"  << "\"";
                    break;
            case init_in_asm:
                insertss << "\"" << "init_in_asm"  << "\"";
                break;
            case success:
                insertss << "\"" << "success"  << "\"";
                break;
            case ignore_analysis:
                insertss << "\"" << "ignore_analysis"  << "\"";
                break;
            default:
                insertss << "\"" << "unknown" << "\"";
                break;
        }

        insertss << "," << Ctx->Global_Func_Alias_Frequency_Map[f].size() << ")";

        //Stop batch collection and build a new batch
        if(batchnum >= batch_size || icallid == icallnum){
            batchnum = 0;

            insertss << ";";
            cmds.push_back(insertss.str());
            insertss.str("");

            if(icallid != icallnum){
                insertss << "insert into func_table ";
                insertss << "(func_name, Alias_result, frequency) values ";
            }
        }
        else{
            insertss << ",";
        }
        icallid++;
    }
}