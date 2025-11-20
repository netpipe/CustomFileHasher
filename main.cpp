// main.cpp
#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QComboBox>
#include <QFileDialog>
#include <QFile>
#include <QCryptographicHash>
#include <QMessageBox>
#include <QTextStream>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QStringList>
#include <QMap>
#include <QVector>

// ----- Wordlist -----
static QMap<QChar, QVector<QString>> letterWordMap{
    {'A', {"APPLE","ATOM","ARROW","ACORN"}},
    {'B', {"BEAR","BOX","BUBBLE","BRIDGE"}},
    {'C', {"CAT","CANYON","CIDER","COMET"}},
    {'D', {"DOG","DUNE","DRAGON","DELTA"}},
    {'E', {"EAGLE","EARTH","EMBER","ENGINE"}},
    {'F', {"FROG","FLAME","FROST","FIDDLE"}},
    {'G', {"GHOST","GALAXY","GEM","GRAPE"}},
    {'H', {"HORSE","HOUSE","HONEY","HUB"}},
    {'I', {"ICE","IGLOO","INK","IRON"}},
    {'J', {"JAZZ","JUMP","JELLY","JUPITER"}},
    {'K', {"KING","KITE","KOALA","KNIGHT"}},
    {'L', {"LION","LASER","LAVA","LEAF"}},
    {'M', {"MOON","MAPLE","MAGIC","MIST"}},
    {'N', {"NOVA","NUT","NIGHT","NEON"}},
    {'O', {"OWL","OCEAN","ORBIT","ONYX"}},
    {'P', {"PIZZA","PANDA","PEAR","PLUTO"}},
    {'Q', {"QUILL","QUAKE","QUARTZ","QUEEN"}},
    {'R', {"ROBOT","RIVER","RUBY","RAIN"}},
    {'S', {"SUN","SPICE","STAR","SAGE"}},
    {'T', {"TACO","TREE","TIGER","TORCH"}},
    {'U', {"UMBER","UNICORN","URANUS","USHER"}},
    {'V', {"VANILLA","VOLCANO","VIOLET","VEX"}},
    {'W', {"WOLF","WATER","WIND","WAX"}},
    {'X', {"XENON","XYLOPHONE","X-RAY","XTRA"}},
    {'Y', {"YAK","YELLOW","YARN","YONDER"}},
    {'Z', {"ZEBRA","ZEUS","ZINC","ZEST"}}
};

// ----- Hash helpers -----
QString makePlate(const QByteArray &hash){
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    QString plate;
    for(int i=0;i<6;i++){
        uchar v=hash[i];
        plate+=alphabet[v%(sizeof(alphabet)-1)];
        if(i==2) plate+='-';
    }
    return plate;
}

QString makeWordHash(const QByteArray &hash){
    int a = hash[0] % 26;
    int b = hash[1] % 26;
    int num = (uchar)hash[2];

    QChar keyA = QChar('A' + a);
    QChar keyB = QChar('A' + b);

    if(!letterWordMap.contains(keyA) || !letterWordMap.contains(keyB))
        return "UNKNOWN-UNKNOWN";

    QVector<QString> listA = letterWordMap[keyA];
    QVector<QString> listB = letterWordMap[keyB];

    if(listA.isEmpty() || listB.isEmpty()) return "UNKNOWN-UNKNOWN";

    QString wordA = listA[a % listA.size()];
    QString wordB = listB[b % listB.size()];

    return QString("%1-%2-%3").arg(wordA).arg(wordB).arg(num);
}

QString decodePhraseToPlate(const QString &phrase)
{
    QString plate;

    QStringList words = phrase.split("-", QString::SkipEmptyParts);

    for(int i = 0; i < words.size(); i++){
        QString word = words[i].toUpper();

        // Find which letter’s list contains this word
        QChar foundLetter = '?';

        for(auto it = letterWordMap.begin(); it != letterWordMap.end(); ++it){
            const QVector<QString> &list = it.value();
            for(const QString &entry : list){
                if(entry == word){
                    foundLetter = it.key();
                    break;
                }
            }
            if(foundLetter != '?') break;
        }

        if(foundLetter == '?'){
            // Unknown word → skip or insert placeholder
            continue;
        }

        // Reverse number mapping: A→0, B→1, …, J→9
        if(foundLetter >= 'A' && foundLetter <= 'J'){
            int digit = foundLetter.unicode() - QChar('A').unicode();
            plate += QString::number(digit);
        } else {
            plate += foundLetter;
        }
    }

    return plate;
}


QString encodePlateToPhrase(const QString &plate){
    QStringList phrase;
    for(int i=0;i<plate.size();i++){
        QChar c = plate[i].toUpper();

        // Map digit to letter (0->A, 1->B, ..., 9->J)
        if(c.isDigit()){
            int num = c.digitValue();
            c = QChar('A' + (num % 26));
        }

        // Skip characters not in the map
        if(!letterWordMap.contains(c)) continue;

        QVector<QString> wlist = letterWordMap[c];
        if(wlist.isEmpty()) continue; // safety check

        int idx = (i*7) % wlist.size(); // deterministic
        phrase.append(wlist[idx]);
    }
    return phrase.join("-");
}


// ----- Hash file -----
QByteArray hashFile(const QString &path, QCryptographicHash::Algorithm algo){
    QFile f(path);
    if(!f.open(QFile::ReadOnly)) return QByteArray();
    QCryptographicHash hash(algo);
    while(!f.atEnd()) hash.addData(f.read(1<<20));
    return hash.result();
}

// ----- Plain-text embedding -----
bool embedPlainText(const QString &src,const QString &funnyHash,const QString &platePhrase, QString &outpath){
    QFile in(src);
    if(!in.open(QFile::ReadOnly)) return false;
    QByteArray data=in.readAll(); in.close();

    outpath=src+".funnycopy";
    QFile out(outpath); if(!out.open(QFile::WriteOnly)) return false;
    out.write(data);
    out.write("\n<!-- FUNNY-HASH: "); out.write(funnyHash.toUtf8()); out.write(" -->\n");
    out.write("<!-- PLATE-PHRASE: "); out.write(platePhrase.toUtf8()); out.write(" -->\n");
    out.close();
    return true;
}

// ----- Qt GUI -----
class FunnyHashWidget: public QWidget{
    Q_OBJECT
public:
    FunnyHashWidget(){
        setAcceptDrops(true);
        QVBoxLayout* layout=new QVBoxLayout(this);

        fileEdit=new QLineEdit; fileEdit->setReadOnly(true); layout->addWidget(fileEdit);

        algoCombo=new QComboBox;
        algoCombo->addItem("MD5", QCryptographicHash::Md5);
        algoCombo->addItem("SHA-1", QCryptographicHash::Sha1);
        algoCombo->addItem("SHA-256", QCryptographicHash::Sha256);
        layout->addWidget(algoCombo);

        plateEdit=new QLineEdit;
        plateEdit->setPlaceholderText("Enter custom plate string (e.g., T3ST1NG)");
        layout->addWidget(plateEdit);

        hashLabel=new QLabel("Funny hash + plate phrase will appear here."); layout->addWidget(hashLabel);

        QPushButton* pickBtn=new QPushButton("Load File"); layout->addWidget(pickBtn);
        QPushButton* genBtn=new QPushButton("Generate Funny Hash & Plate Phrase"); layout->addWidget(genBtn);
        QPushButton* embedBtn=new QPushButton("Embed Hash & Phrase"); layout->addWidget(embedBtn);
        QPushButton* verifyBtn=new QPushButton("Verify Embedded Data"); layout->addWidget(verifyBtn);

        connect(pickBtn,&QPushButton::clicked,[&](){
            QString f=QFileDialog::getOpenFileName(this,"Choose file");
            if(!f.isEmpty()){ selectedFile=f; fileEdit->setText(f); }
        });

        connect(genBtn,&QPushButton::clicked,[&](){
            if(selectedFile.isEmpty()){ QMessageBox::warning(this,"Error","Load a file first."); return; }
            QCryptographicHash::Algorithm algo=(QCryptographicHash::Algorithm)algoCombo->currentData().toInt();
            QFile file(selectedFile); if(!file.open(QFile::ReadOnly)){ QMessageBox::warning(this,"Error","Cannot read file."); return; }
            QByteArray data=file.readAll(); file.close();
            QByteArray h=QCryptographicHash::hash(data,algo);
            currentFunnyHash=makePlate(h)+" | "+makeWordHash(h);

            QString plateStr=plateEdit->text().isEmpty() ? makePlate(h) : plateEdit->text();
            currentPlatePhrase=encodePlateToPhrase(plateStr);

            hashLabel->setText("Funny Hash:\n"+currentFunnyHash+"\nPlate Phrase:\n"+currentPlatePhrase);
        });

        connect(embedBtn,&QPushButton::clicked,[&](){
            if(selectedFile.isEmpty()||currentFunnyHash.isEmpty()||currentPlatePhrase.isEmpty()){
                QMessageBox::warning(this,"Error","Load file and generate hash first."); return;
            }
            QString out;
            if(embedPlainText(selectedFile,currentFunnyHash,currentPlatePhrase,out))
                QMessageBox::information(this,"Done","Embedded file created:\n"+out);
            else QMessageBox::warning(this,"Error","Failed to embed hash/phrase.");
        });

        connect(verifyBtn,&QPushButton::clicked,[&](){
            if(selectedFile.isEmpty()){ QMessageBox::warning(this,"Error","Load a file first."); return; }
            QFile f(selectedFile); if(!f.open(QFile::ReadOnly)) return;
            QByteArray data=f.readAll(); f.close();
            bool found=false;
            if(data.contains(currentFunnyHash.toUtf8()) && data.contains(currentPlatePhrase.toUtf8())) found=true;
            QMessageBox::information(this,"Verify",found?"Data FOUND inside file!":"Data NOT found.");
        });
    }

protected:
    void dragEnterEvent(QDragEnterEvent *event) override { if(event->mimeData()->hasUrls()) event->acceptProposedAction(); }
    void dropEvent(QDropEvent *event) override {
        QList<QUrl> urls=event->mimeData()->urls(); if(urls.isEmpty()) return;
        QString path=urls.first().toLocalFile(); if(!path.isEmpty()){ selectedFile=path; fileEdit->setText(path); }
    }

private:
    QLineEdit *fileEdit, *plateEdit;
    QLabel *hashLabel;
    QComboBox *algoCombo;
    QString selectedFile, currentFunnyHash, currentPlatePhrase;
};



int main(int argc,char* argv[]){
    QApplication a(argc,argv);
    FunnyHashWidget w; w.resize(600,400); w.setWindowTitle("Reversible Plate → Phrase Funny Hash Generator");
    w.show();
    return a.exec();
}
#include "main.moc"
